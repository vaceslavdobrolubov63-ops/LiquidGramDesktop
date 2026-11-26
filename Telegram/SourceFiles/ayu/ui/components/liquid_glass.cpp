// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/liquid_glass.h"

#include "base/debug_log.h"
#include "ui/gl/gl_detection.h"
#include "ui/gl/gl_surface.h"
#include "ui/painter.h"
#include "ui/power_saving.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include "ui/rhi/rhi_renderer.h"
#include "ui/rhi/rhi_shader.h"

#include <rhi/qrhi.h>
#endif // Qt >= 6.7

#include <QtGui/QPainter>

#include <algorithm>
#include <cstring>

namespace Ayu::Ui {
namespace {

constexpr auto kQuadVertexCount = 6;
constexpr auto kCaptureDebounceMs = crl::time(16);

const float kQuadVertices[kQuadVertexCount * 2] = {
	0.f, 0.f,
	1.f, 0.f,
	0.f, 1.f,
	1.f, 0.f,
	0.f, 1.f,
	1.f, 1.f,
};

struct alignas(16) LiquidGlassUniforms {
	float rectSizeAndDpr[4];
	float glassParams[4];
	float tintColor[4];
	float lightAndFlags[4];
};
static_assert(sizeof(LiquidGlassUniforms) == 64);

[[nodiscard]] QImage NormalizeBackdrop(QImage image) {
	if (image.isNull()) {
		auto fallback = QImage(2, 2, QImage::Format_RGBA8888);
		fallback.fill(QColor(24, 24, 28, 255));
		return fallback;
	}
	if (image.format() != QImage::Format_RGBA8888) {
		return image.convertToFormat(QImage::Format_RGBA8888);
	}
	return image;
}

} // namespace

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

class LiquidGlassWidget::Renderer final
	: public ::Ui::GL::Renderer
	, public ::Ui::Rhi::Renderer {
public:
	Renderer() = default;
	~Renderer() override {
		releaseResources();
	}

	void setStyle(const Style &style, QSize logicalSize) {
		_style = style;
		_logicalSize = logicalSize;
	}

	void setBackdrop(QImage image) {
		_pendingBackdrop = NormalizeBackdrop(std::move(image));
		_backdropDirty = true;
	}

	void initialize(
			QRhi *rhi,
			QRhiRenderTarget *rt,
			QRhiCommandBuffer *cb) override {
		if (_initialized && _rhi == rhi) {
			return;
		}
		if (_rhi != rhi) {
			_creationFailed = false;
		}
		releaseResources();
		if (_creationFailed) {
			return;
		}
		_rhi = rhi;

		_vertexBuffer = rhi->newBuffer(
			QRhiBuffer::Immutable,
			QRhiBuffer::VertexBuffer,
			sizeof(kQuadVertices));
		if (!_vertexBuffer->create()) {
			_creationFailed = true;
			releaseResources();
			return;
		}

		_uniformBuffer = rhi->newBuffer(
			QRhiBuffer::Dynamic,
			QRhiBuffer::UniformBuffer,
			sizeof(LiquidGlassUniforms));
		if (!_uniformBuffer->create()) {
			_creationFailed = true;
			releaseResources();
			return;
		}

		_sampler = rhi->newSampler(
			QRhiSampler::Linear,
			QRhiSampler::Linear,
			QRhiSampler::None,
			QRhiSampler::ClampToEdge,
			QRhiSampler::ClampToEdge);
		if (!_sampler->create()) {
			_creationFailed = true;
			releaseResources();
			return;
		}

		if (_pendingBackdrop.isNull()) {
			_pendingBackdrop = NormalizeBackdrop({});
		}
		_currentBackdropSize = _pendingBackdrop.size();
		_backdropTexture = rhi->newTexture(
			QRhiTexture::RGBA8,
			_currentBackdropSize);
		if (!_backdropTexture->create()) {
			_creationFailed = true;
			releaseResources();
			return;
		}

		if (!createPipeline(rt)) {
			LOG(("LiquidGlass: pipeline creation failed."));
			_creationFailed = true;
			releaseResources();
			return;
		}

		auto *rub = rhi->nextResourceUpdateBatch();
		rub->uploadStaticBuffer(_vertexBuffer, kQuadVertices);
		rub->uploadTexture(_backdropTexture, _pendingBackdrop);
		cb->resourceUpdate(rub);
		_backdropDirty = false;
		_initialized = true;
	}

	void render(
			QRhi *rhi,
			QRhiRenderTarget *rt,
			QRhiCommandBuffer *cb) override {
		if (rhi->isDeviceLost()) {
			releaseResources();
			return;
		}
		if (!_initialized) {
			cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 });
			cb->endPass();
			return;
		}
		_rhi = rhi;

		const auto pixelSize = rt->pixelSize();
		if (pixelSize.isEmpty()) {
			return;
		}

		auto *rub = rhi->nextResourceUpdateBatch();
		if (_backdropDirty && !_pendingBackdrop.isNull()) {
			if (_pendingBackdrop.size() != _currentBackdropSize) {
				delete _backdropTexture;
				_backdropTexture = rhi->newTexture(
					QRhiTexture::RGBA8,
					_pendingBackdrop.size());
				if (!_backdropTexture->create() || !recreateBindings()) {
					releaseResources();
					return;
				}
				_currentBackdropSize = _pendingBackdrop.size();
			}
			rub->uploadTexture(_backdropTexture, _pendingBackdrop);
			_backdropDirty = false;
		}

		const auto logicalW = float(std::max(_logicalSize.width(), 1));
		const auto logicalH = float(std::max(_logicalSize.height(), 1));
		const auto texW = float(std::max(_currentBackdropSize.width(), 1));
		const auto texH = float(std::max(_currentBackdropSize.height(), 1));

		auto uni = LiquidGlassUniforms();
		uni.rectSizeAndDpr[0] = logicalW;
		uni.rectSizeAndDpr[1] = logicalH;
		uni.rectSizeAndDpr[2] = texW;
		uni.rectSizeAndDpr[3] = texH;
		uni.glassParams[0] = _style.cornerRadius;
		uni.glassParams[1] = _style.blurStrength;
		uni.glassParams[2] = _style.distortionStrength;
		uni.glassParams[3] = _style.opacity;
		uni.tintColor[0] = float(_style.tintColor.redF());
		uni.tintColor[1] = float(_style.tintColor.greenF());
		uni.tintColor[2] = float(_style.tintColor.blueF());
		uni.tintColor[3] = _style.tintStrength;
		uni.lightAndFlags[0] = float(_style.lightDirection.x());
		uni.lightAndFlags[1] = float(_style.lightDirection.y());
		uni.lightAndFlags[2] = rhi->isYUpInNDC() ? 1.f : -1.f;
		uni.lightAndFlags[3] = 0.f;

		rub->updateDynamicBuffer(_uniformBuffer, 0, sizeof(uni), &uni);

		cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 }, rub);
		cb->setGraphicsPipeline(_pipeline);
		cb->setViewport({
			0.f,
			0.f,
			float(pixelSize.width()),
			float(pixelSize.height()) });
		cb->setShaderResources(_srb);
		const QRhiCommandBuffer::VertexInput vbuf[] = {
			{ _vertexBuffer, 0 },
		};
		cb->setVertexInput(0, 1, vbuf);
		cb->draw(kQuadVertexCount);
		cb->endPass();
	}

	void releaseResources() override {
		if (!_rhi) {
			return;
		}
		delete _pipeline;
		_pipeline = nullptr;
		delete _srb;
		_srb = nullptr;
		delete _sampler;
		_sampler = nullptr;
		delete _backdropTexture;
		_backdropTexture = nullptr;
		delete _uniformBuffer;
		_uniformBuffer = nullptr;
		delete _vertexBuffer;
		_vertexBuffer = nullptr;
		_initialized = false;
		_rhi = nullptr;
	}

	[[nodiscard]] QColor rhiClearColor() override {
		return QColor(0, 0, 0, 0);
	}

	[[nodiscard]] std::optional<QColor> clearColor() override {
		return QColor(0, 0, 0, 0);
	}

private:
	[[nodiscard]] bool recreateBindings() {
		delete _srb;
		_srb = _rhi->newShaderResourceBindings();
		_srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer),
			QRhiShaderResourceBinding::sampledTexture(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				_backdropTexture,
				_sampler),
		});
		return _srb->create();
	}

	[[nodiscard]] bool createPipeline(QRhiRenderTarget *rt) {
		const auto vert = ::Ui::Rhi::ShaderFromFile(
			u":/shaders/liquid_glass.vert.qsb"_q);
		const auto frag = ::Ui::Rhi::ShaderFromFile(
			u":/shaders/liquid_glass.frag.qsb"_q);
		if (!vert.isValid() || !frag.isValid()) {
			return false;
		}
		if (!recreateBindings()) {
			return false;
		}

		_pipeline = _rhi->newGraphicsPipeline();
		_pipeline->setShaderStages({
			{ QRhiShaderStage::Vertex, vert },
			{ QRhiShaderStage::Fragment, frag },
		});

		QRhiVertexInputLayout inputLayout;
		inputLayout.setBindings({
			{ quint32(2 * sizeof(float)) },
		});
		inputLayout.setAttributes({
			{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		});
		_pipeline->setVertexInputLayout(inputLayout);

		QRhiGraphicsPipeline::TargetBlend blend;
		blend.enable = true;
		blend.srcColor = QRhiGraphicsPipeline::One;
		blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
		blend.srcAlpha = QRhiGraphicsPipeline::One;
		blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
		_pipeline->setTargetBlends({ blend });

		_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
		_pipeline->setCullMode(QRhiGraphicsPipeline::None);
		_pipeline->setDepthTest(false);
		_pipeline->setDepthWrite(false);
		_pipeline->setSampleCount(rt->sampleCount());
		_pipeline->setShaderResourceBindings(_srb);
		_pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());
		return _pipeline->create();
	}

	QRhi *_rhi = nullptr;
	QRhiBuffer *_vertexBuffer = nullptr;
	QRhiBuffer *_uniformBuffer = nullptr;
	QRhiTexture *_backdropTexture = nullptr;
	QRhiSampler *_sampler = nullptr;
	QRhiShaderResourceBindings *_srb = nullptr;
	QRhiGraphicsPipeline *_pipeline = nullptr;

	Style _style;
	QSize _logicalSize;
	QImage _pendingBackdrop;
	QSize _currentBackdropSize;
	bool _backdropDirty = false;
	bool _initialized = false;
	bool _creationFailed = false;
};

#else // Qt >= 6.7

class LiquidGlassWidget::Renderer final : public ::Ui::GL::Renderer {
public:
	void setStyle(const Style &, QSize) {}
	void setBackdrop(QImage) {}
};

#endif // Qt >= 6.7

LiquidGlassWidget::LiquidGlassWidget(QWidget *parent, Style style)
: RpWidget(parent)
, _style(std::move(style))
, _captureTimer([=] { captureFromParent(); }) {
	setAttribute(Qt::WA_TranslucentBackground);
	ensureSurface();
	updateSurfaceState();
}

LiquidGlassWidget::~LiquidGlassWidget() = default;

bool LiquidGlassWidget::Supported() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (::Ui::PowerSaving::On(::Ui::PowerSaving::kAnimations)) {
		return false;
	}
	return ::Ui::GL::WidgetsRhiSupported();
#else
	return false;
#endif
}

void LiquidGlassWidget::setStyle(Style style) {
	_style = std::move(style);
	updateSurfaceState();
}

void LiquidGlassWidget::setCornerRadius(float radius) {
	_style.cornerRadius = radius;
	updateSurfaceState();
}

void LiquidGlassWidget::setBlurStrength(float strength) {
	_style.blurStrength = strength;
	updateSurfaceState();
}

void LiquidGlassWidget::setDistortionStrength(float strength) {
	_style.distortionStrength = strength;
	updateSurfaceState();
}

void LiquidGlassWidget::setOpacity(float opacity) {
	_style.opacity = std::clamp(opacity, 0.f, 1.f);
	updateSurfaceState();
}

void LiquidGlassWidget::setTintColor(QColor color, float strength) {
	_style.tintColor = color;
	_style.tintStrength = std::clamp(strength, 0.f, 1.f);
	updateSurfaceState();
}

void LiquidGlassWidget::setLightDirection(QPointF dir) {
	_style.lightDirection = dir;
	updateSurfaceState();
}

void LiquidGlassWidget::setBackdropImage(QImage image) {
	_backdrop = std::move(image);
	if (_renderer) {
		_renderer->setBackdrop(_backdrop);
		if (const auto w = surfaceWidget()) {
			w->update();
		}
	} else {
		update();
	}
}

void LiquidGlassWidget::setAutoCaptureParent(bool enabled) {
	_autoCaptureParent = enabled;
	if (_autoCaptureParent) {
		scheduleCapture();
	} else {
		_captureTimer.cancel();
	}
}

void LiquidGlassWidget::setCaptureSource(QWidget *source) {
	_captureSource = source;
	if (_autoCaptureParent) {
		scheduleCapture();
	}
}

void LiquidGlassWidget::refreshBackdrop() {
	if (_autoCaptureParent) {
		scheduleCapture();
	}
}

QWidget *LiquidGlassWidget::surfaceWidget() const {
	return _surface ? _surface->rpWidget() : nullptr;
}

void LiquidGlassWidget::ensureSurface() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (_surface || !Supported()) {
		return;
	}
	auto renderer = std::make_unique<Renderer>();
	_renderer = renderer.get();
	_surface = ::Ui::GL::CreateSurface(
		this,
		::Ui::GL::ChosenRenderer{
			.renderer = std::move(renderer),
			.backend = ::Ui::GL::Backend::QRhi,
		});
	if (const auto w = surfaceWidget()) {
		w->setAttribute(Qt::WA_TransparentForMouseEvents);
		w->setGeometry(rect());
		w->lower();
		w->show();
	}
#endif // Qt >= 6.7
}

void LiquidGlassWidget::updateSurfaceState() {
	if (_renderer) {
		_renderer->setStyle(_style, size());
		if (const auto w = surfaceWidget()) {
			w->update();
		}
	} else {
		update();
	}
}

void LiquidGlassWidget::scheduleCapture() {
	if (!_autoCaptureParent || isHidden() || _capturing) {
		return;
	}
	if (!_captureTimer.isActive()) {
		_captureTimer.callOnce(kCaptureDebounceMs);
	}
}

void LiquidGlassWidget::captureFromParent() {
	const auto source = _captureSource
		? _captureSource.data()
		: parentWidget();
	if (!_autoCaptureParent || !source || isHidden() || size().isEmpty()) {
		return;
	}
	_capturing = true;
	const auto wasVisible = isVisible();
	if (wasVisible) {
		setAttribute(Qt::WA_DontShowOnScreen, true);
		hide();
	}

	const auto dpr = devicePixelRatioF();
	auto captured = QImage(
		QSizeF(size() * dpr).toSize(),
		QImage::Format_RGBA8888_Premultiplied);
	captured.setDevicePixelRatio(dpr);
	captured.fill(Qt::transparent);

	const auto originInSource = mapTo(source, QPoint(0, 0));
	QPainter painter(&captured);
	source->render(
		&painter,
		QPoint(),
		QRegion(QRect(originInSource, size())),
		QWidget::DrawWindowBackground | QWidget::DrawChildren);
	painter.end();

	if (wasVisible) {
		show();
		setAttribute(Qt::WA_DontShowOnScreen, false);
	}
	_capturing = false;

	setBackdropImage(std::move(captured));
}

void LiquidGlassWidget::paintEvent(QPaintEvent *e) {
	if (surfaceWidget()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	auto fill = _style.tintColor;
	fill.setAlphaF(std::clamp(_style.tintStrength + 0.12f, 0.08f, 0.65f)
		* _style.opacity);
	p.setPen(QPen(QColor(255, 255, 255, int(70 * _style.opacity)), 1.0));
	p.setBrush(fill);
	p.drawRoundedRect(
		QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
		_style.cornerRadius,
		_style.cornerRadius);
}

void LiquidGlassWidget::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	if (const auto w = surfaceWidget()) {
		w->setGeometry(rect());
	}
	updateSurfaceState();
	scheduleCapture();
}

void LiquidGlassWidget::moveEvent(QMoveEvent *e) {
	RpWidget::moveEvent(e);
	scheduleCapture();
}

void LiquidGlassWidget::showEvent(QShowEvent *e) {
	RpWidget::showEvent(e);
	updateSurfaceState();
	scheduleCapture();
}

} // namespace Ayu::Ui
