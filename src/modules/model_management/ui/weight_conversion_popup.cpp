#include "modules/model_management/ui/weight_conversion_popup.h"

#include "modules/model_management/application/weight_conversion.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

namespace studio {

WeightConversionPopup::WeightConversionPopup(
    const jocelin::model::ModelEntry& model,
    const jocelin::model::ModelWeight& source,
    QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName("weightConversionPopup");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral(R"QSS(
QDialog#weightConversionPopup { background: #FFFFFF; border: 1px solid #C8D6DA; border-radius: 8px; }
QWidget#weightConversionDevices { background: transparent; }
QPushButton[conversionChoice=true] { min-width: 70px; padding: 7px 10px; text-align: center; }
QPushButton[conversionChoice=true]:checked { background: #DDF5EE; border-color: #0EA67A; color: #087A5C; font-weight: 700; }
QPushButton[conversionChoice=true][conversionUnavailable=true] { color: #92A19F; }
QPushButton[primary=true] { background: #0EA67A; color: white; border-color: #0A8E69; text-align: center; }
QPushButton[primary=true]:disabled { background: #F4F7F8; color: #8A9B99; border-color: #DCE4E7; }
)QSS"));
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    QHBoxLayout* formats = new QHBoxLayout;
    QButtonGroup* group = new QButtonGroup(this);
    QPushButton* onnx = new QPushButton(tr("ONNX"), this);
    QPushButton* tensorrt = new QPushButton(tr("TensorRT"), this);
    QPushButton* openvino = new QPushButton(tr("OpenVINO"), this);
    onnx->setObjectName("weightConversionOnnx");
    tensorrt->setObjectName("weightConversionTensorRt");
    openvino->setObjectName("weightConversionOpenVino");
    for (QPushButton* button : {onnx, tensorrt, openvino}) {
        button->setCheckable(true);
        button->setProperty("conversionChoice", true);
        group->addButton(button);
        formats->addWidget(button);
    }
    root->addLayout(formats);

    QWidget* devices = new QWidget(this);
    devices->setObjectName("weightConversionDevices");
    QHBoxLayout* device_layout = new QHBoxLayout(devices);
    device_layout->setContentsMargins(0, 0, 0, 0);
    QPushButton* cpu = new QPushButton(tr("CPU"), devices);
    QPushButton* gpu = new QPushButton(tr("GPU"), devices);
    cpu->setObjectName("weightConversionCpu");
    gpu->setObjectName("weightConversionGpu");
    cpu->setCheckable(true);
    gpu->setCheckable(true);
    cpu->setProperty("conversionChoice", true);
    gpu->setProperty("conversionChoice", true);
    QButtonGroup* device_group = new QButtonGroup(this);
    device_group->addButton(cpu);
    device_group->addButton(gpu);
    cpu->setEnabled(false);
    gpu->setChecked(true);
    device_layout->addWidget(cpu);
    device_layout->addWidget(gpu);
    root->addWidget(devices);

    QHBoxLayout* actions = new QHBoxLayout;
    actions->addStretch();
    QPushButton* convert = new QPushButton(tr("Convert"), this);
    convert->setObjectName("weightConversionConfirm");
    convert->setProperty("primary", true);
    actions->addWidget(convert);
    root->addLayout(actions);

    using jocelin::config::WeightType;
    const bool onnx_supported = can_convert_weight(model, source, WeightType::Onnx);
    const bool openvino_supported = can_convert_weight(model, source, WeightType::OpenVino);
    const QString unavailable = tr("Conversion is not available for this model or format.");
    if (!onnx_supported) {
        onnx->setToolTip(unavailable);
        onnx->setProperty("conversionUnavailable", true);
    }
    tensorrt->setToolTip(unavailable);
    tensorrt->setProperty("conversionUnavailable", true);
    if (!openvino_supported) {
        openvino->setToolTip(unavailable);
        openvino->setProperty("conversionUnavailable", true);
    }

    const auto update = [=] {
        const bool use_openvino = openvino->isChecked();
        const bool use_tensorrt = tensorrt->isChecked();
        target_format_ = use_openvino ? WeightType::OpenVino :
            (use_tensorrt ? WeightType::TensorRt : WeightType::Onnx);
        devices->setVisible(use_openvino || use_tensorrt);
        cpu->setText(use_openvino ? tr("CPU / FP32") : tr("CPU"));
        gpu->setVisible(use_tensorrt);
        if (use_openvino) {
            cpu->setChecked(true);
        } else if (use_tensorrt) {
            gpu->setChecked(true);
        }
        convert->setEnabled((onnx->isChecked() && onnx_supported) || (use_openvino && openvino_supported));
        convert->setToolTip(convert->isEnabled() ? QString{} : unavailable);
        adjustSize();
    };
    connect(group, qOverload<QAbstractButton*>(&QButtonGroup::buttonClicked), this,
        [update](QAbstractButton*) { update(); });
    connect(convert, &QPushButton::clicked, this, &QDialog::accept);
    onnx->setChecked(true);
    update();
}

} // namespace studio
