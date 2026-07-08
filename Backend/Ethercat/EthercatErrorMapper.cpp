#include "EthercatErrorMapper.h"

namespace Backend {

QString toUserMessage(soem_interface::error::SoemInterfaceErrorCode code)
{
    using namespace soem_interface::error;

    switch (code) {
    case NoError:
        return QStringLiteral("无错误");
    case InvalidSlave:
        return QStringLiteral("从站数量不一致,请检查配置文件!");
    case InvalidNicName:
        return QStringLiteral("网卡名称错误");
    case NoSlaveFound:
        return QStringLiteral("未找到从站");
    case EcatInitFailed:
        return QStringLiteral("EtherCAT初始化失败，网卡选择错误或权限不足");
    case RequestOpFailed:
        return QStringLiteral("请求OP状态失败");
    case RxPdoSizeMismatch:
        return QStringLiteral("RxPDO大小不匹配");
    case TxPdoSizeMismatch:
        return QStringLiteral("TxPDO大小不匹配");
    case EthercatNotOperational:
        return QStringLiteral("EtherCAT未运行");
    case InvaidEEpromHexFile:
        return QStringLiteral("无效HEX文件");
    case InvaidEEpromBinFile:
        return QStringLiteral("无效Bin文件");
    }

    return QStringLiteral("未知错误");
}

} // namespace Backend
