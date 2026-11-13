#include "SOEM_interface/SoemUtils.h"
#include "SOEM_interface/ErrorText.h"

#include <iomanip>
#include <sstream>

namespace soem_interface {
namespace {

const char* phaseText(FirmwareFlashError error)
{
    switch (error) {
    case FirmwareFlashError::None: return "下载确认";
    case FirmwareFlashError::InvalidFile:
    case FirmwareFlashError::InvalidPackage: return "固件文件检查";
    case FirmwareFlashError::InvalidSlave: return "从站选择";
    case FirmwareFlashError::OpenFailed: return "网卡连接";
    case FirmwareFlashError::ScanFailed: return "从站初始化";
    case FirmwareFlashError::PreOpFailed: return "PRE-OP 状态确认";
    case FirmwareFlashError::InitFailed: return "INIT 状态切换";
    case FirmwareFlashError::InvalidBootMailbox:
    case FirmwareFlashError::BootMailboxWriteFailed: return "BOOT 邮箱配置";
    case FirmwareFlashError::BootFailed: return "BOOT 状态切换";
    case FirmwareFlashError::TransferFailed: return "FoE 数据传输";
    case FirmwareFlashError::ActivationFailed: return "退出 BOOT 请求";
    }
    return "未知阶段";
}

std::string hexCode(uint32_t code, int width)
{
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << code;
    return out.str();
}

std::string stateText(uint16_t state)
{
    std::string name;
    switch (state & 0xf) {
    case EC_STATE_INIT: name = "INIT"; break;
    case EC_STATE_PRE_OP: name = "PRE-OP"; break;
    case EC_STATE_BOOT: name = "BOOT"; break;
    case EC_STATE_SAFE_OP: name = "SAFE-OP"; break;
    case EC_STATE_OPERATIONAL: name = "OP"; break;
    default: name = "未知状态"; break;
    }
    if (state & EC_STATE_ERROR) name += " + ERROR";
    return name + "（" + hexCode(state, 4) + "）";
}

} // namespace

std::string formatFirmwareFlashResult(const FirmwareFlashResult& result)
{
    std::ostringstream out;
    if (result.error == FirmwareFlashError::None) {
        if (!result.downloadComplete)
            return "尚未执行固件下载\n从站：" + std::to_string(result.slave);
        out << "固件下载已确认\n从站：" << result.slave
            << "\n\n处理结果\n已收到固件数据的最终应答。";
        if (result.activationRequested)
            out << "\n已发送退出 BOOT 请求，设备可能重启。";
        out << "\n\n后续确认\n重新连接后核对安装结果和固件版本。";
        return out.str();
    }
    out << (result.downloadComplete ? "固件已下载，激活请求失败" : "固件下载失败")
        << " [FW" << std::setfill('0') << std::setw(3) << static_cast<int>(result.error) << "]"
        << "\n从站：" << result.slave << " · 阶段：" << phaseText(result.error)
        << "\n\n失败原因\n" << result.reason;
    if (result.foeError)
        out << "\n从站说明：" << foeErrorText(result.foeError);
    if (result.mailboxError)
        out << "\n邮箱说明：" << mailboxErrorText(result.mailboxError);

    const bool localFailure = result.error == FirmwareFlashError::InvalidFile
        || result.error == FirmwareFlashError::InvalidPackage
        || result.error == FirmwareFlashError::InvalidSlave;
    if (!localFailure) {
        out << "\n\n错误码\nSOEM 返回值：" << result.soemReturn;
        if (result.foeError) out << "\nFoE 错误码：" << hexCode(result.foeError, 8);
        if (result.mailboxError) out << "\nMBX 错误码：" << hexCode(result.mailboxError, 4);
    }
    if (result.alStateValid) {
        out << "\n\n从站状态\nAL 状态：" << stateText(result.alState)
            << "\nAL 错误码：" << hexCode(result.alStatusCode, 4)
            << "（" << alStatusText(result.alStatusCode) << "）";
    } else if (!localFailure && result.error != FirmwareFlashError::OpenFailed) {
        out << "\n\n从站状态\nAL 状态：未能回读";
    }
    if (result.error == FirmwareFlashError::TransferFailed)
        out << "\n\n处理说明\n未发送退出 BOOT 请求，便于保留失败现场。";
    if (result.downloadComplete)
        out << "\n\n处理说明\n固件数据已获最终应答，安装与启动结果尚未确认。";
    return out.str();
}

} // namespace soem_interface
