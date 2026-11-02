#include "resource_monitor.h"

#include <QDir>
#include <QLabel>
#include <QLibrary>
#include <QSettings>
#include <QSizePolicy>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

#if defined(Q_OS_WIN)
#include <Windows.h>
#if __has_include(<nvml.h>)
#include <nvml.h>
#define STUDIO_HAS_NVML 1
#endif
#endif

namespace studio {

ResourceMonitorWidget::ResourceMonitorWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("resourceMonitor");
    setProperty("sectionPanel", true);
    setMinimumWidth(0);
    setMinimumHeight(110);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(2);
    QLabel* title = new QLabel(tr("SYSTEM RESOURCES"), this);
    title->setProperty("eyebrow", true);
    layout->addWidget(title);

    QString cpu_name = tr("CPU");
#if defined(Q_OS_WIN)
    QSettings processor(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
        QSettings::NativeFormat);
    const QString processor_name = processor.value(QStringLiteral("ProcessorNameString")).toString().trimmed();
    if (!processor_name.isEmpty()) {
        cpu_name = processor_name;
    }
#endif
    QLabel* cpu = new QLabel(cpu_name, this);
    cpu->setObjectName("resourceCpuName");
    cpu->setProperty("meta", true);
    cpu->setWordWrap(true);
    cpu->setMinimumWidth(0);
    cpu->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    cpu->setToolTip(cpu_name);
    layout->addWidget(cpu);
    QLabel* cpu_usage = new QLabel(tr("CPU: —   RAM: —"), this);
    cpu_usage->setObjectName("resourceCpuUsage");
    cpu_usage->setWordWrap(true);
    layout->addWidget(cpu_usage);
    QLabel* gpu_usage = new QLabel(tr("NVIDIA GPU: —"), this);
    gpu_usage->setObjectName("resourceGpuUsage");
    gpu_usage->setWordWrap(true);
    gpu_usage->setMinimumWidth(0);
    gpu_usage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(gpu_usage);
    layout->addStretch();
    setToolTip(tr("System-wide utilization; not limited to this application."));

    std::function<QString()> sample_gpu = [this] { return tr("NVIDIA GPU: unavailable"); };
#if defined(STUDIO_HAS_NVML)
    QLibrary* library = new QLibrary(QStringLiteral("nvml"), this);
    if (!library->load()) {
        const QString program_files = qEnvironmentVariable("ProgramW6432");
        if (!program_files.isEmpty()) {
            library->setFileName(QDir(program_files).filePath(QStringLiteral("NVIDIA Corporation/NVSMI/nvml.dll")));
            library->load();
        }
    }
    using NvmlInit = decltype(&nvmlInit_v2);
    using NvmlShutdown = decltype(&nvmlShutdown);
    using NvmlCount = decltype(&nvmlDeviceGetCount_v2);
    using NvmlHandle = decltype(&nvmlDeviceGetHandleByIndex_v2);
    using NvmlName = decltype(&nvmlDeviceGetName);
    using NvmlUtilization = decltype(&nvmlDeviceGetUtilizationRates);
    using NvmlMemory = decltype(&nvmlDeviceGetMemoryInfo);
    const NvmlInit initialize = reinterpret_cast<NvmlInit>(library->resolve("nvmlInit_v2"));
    const NvmlShutdown shutdown = reinterpret_cast<NvmlShutdown>(library->resolve("nvmlShutdown"));
    const NvmlCount get_count = reinterpret_cast<NvmlCount>(library->resolve("nvmlDeviceGetCount_v2"));
    const NvmlHandle get_handle = reinterpret_cast<NvmlHandle>(library->resolve("nvmlDeviceGetHandleByIndex_v2"));
    const NvmlName get_name = reinterpret_cast<NvmlName>(library->resolve("nvmlDeviceGetName"));
    const NvmlUtilization get_utilization = reinterpret_cast<NvmlUtilization>(library->resolve("nvmlDeviceGetUtilizationRates"));
    const NvmlMemory get_memory = reinterpret_cast<NvmlMemory>(library->resolve("nvmlDeviceGetMemoryInfo"));
    if (initialize && shutdown && get_count && get_handle && get_name && get_utilization && get_memory &&
        initialize() == NVML_SUCCESS) {
        connect(this, &QObject::destroyed, library, [shutdown] { shutdown(); });
        sample_gpu = [this, get_count, get_handle, get_name, get_utilization, get_memory] {
            unsigned int count = 0;
            if (get_count(&count) != NVML_SUCCESS || count == 0) {
                return tr("NVIDIA GPU: unavailable");
            }
            QStringList rows;
            for (unsigned int index = 0; index < count; ++index) {
                nvmlDevice_t device = nullptr;
                if (get_handle(index, &device) != NVML_SUCCESS) {
                    rows.push_back(tr("GPU %1: unavailable").arg(index));
                    continue;
                }
                char name[NVML_DEVICE_NAME_BUFFER_SIZE] = {};
                const QString device_name = get_name(device, name, static_cast<unsigned int>(sizeof(name))) == NVML_SUCCESS
                    ? QString::fromUtf8(name) : tr("GPU %1").arg(index);
                nvmlUtilization_t utilization{};
                const QString load = get_utilization(device, &utilization) == NVML_SUCCESS
                    ? QString::number(utilization.gpu) + QStringLiteral("%") : QStringLiteral("—");
                nvmlMemory_t memory{};
                QString vram = QStringLiteral("—");
                if (get_memory(device, &memory) == NVML_SUCCESS && memory.total > 0) {
                    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
                    vram = tr("%1 / %2 GiB").arg(static_cast<double>(memory.used) / gib, 0, 'f', 1)
                        .arg(static_cast<double>(memory.total) / gib, 0, 'f', 1);
                }
                rows.push_back(tr("%1\nGPU: %2   VRAM: %3").arg(device_name, load, vram));
            }
            return rows.join(QStringLiteral("\n"));
        };
    }
#endif

    QTimer* timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, this,
        [this, cpu_usage, gpu_usage, sample_gpu, previous_idle = quint64{0}, previous_total = quint64{0}]() mutable {
            if (!isVisible() || width() == 0 || height() == 0) {
                previous_idle = 0;
                previous_total = 0;
                return;
            }
            QString cpu_load = QStringLiteral("—");
            QString ram = QStringLiteral("—");
#if defined(Q_OS_WIN)
            FILETIME idle_time{}, kernel_time{}, user_time{};
            if (GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
                const quint64 idle = (quint64(idle_time.dwHighDateTime) << 32) | idle_time.dwLowDateTime;
                const quint64 kernel = (quint64(kernel_time.dwHighDateTime) << 32) | kernel_time.dwLowDateTime;
                const quint64 user = (quint64(user_time.dwHighDateTime) << 32) | user_time.dwLowDateTime;
                const quint64 total = kernel + user;
                if (previous_total != 0 && total > previous_total && idle >= previous_idle) {
                    const quint64 elapsed = total - previous_total;
                    const quint64 idle_elapsed = idle - previous_idle;
                    if (idle_elapsed <= elapsed) {
                        cpu_load = QString::number(100.0 * static_cast<double>(elapsed - idle_elapsed) /
                            static_cast<double>(elapsed), 'f', 1) + QStringLiteral("%");
                    }
                }
                previous_idle = idle;
                previous_total = total;
            }
            MEMORYSTATUSEX memory{};
            memory.dwLength = static_cast<DWORD>(sizeof(memory));
            if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys > 0) {
                constexpr double gib = 1024.0 * 1024.0 * 1024.0;
                ram = tr("%1 / %2 GiB")
                    .arg(static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys) / gib, 0, 'f', 1)
                    .arg(static_cast<double>(memory.ullTotalPhys) / gib, 0, 'f', 1);
            }
#endif
            cpu_usage->setText(tr("CPU: %1   RAM: %2").arg(cpu_load, ram));
            gpu_usage->setText(sample_gpu());
        });
    timer->start();
}

} // namespace studio
