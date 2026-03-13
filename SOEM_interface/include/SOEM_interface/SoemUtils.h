#ifndef SOEMUTILS_H
#define SOEMUTILS_H

#include <string>
#include <vector>
#include "SOEM_interface/soem_interface_export.h"


namespace soem_interface{
    /**
     * @brief SoemUtils
     *
     * 纯工具类：
     * - 不保存任何状态
     * - 不管理 EtherCAT 主站
     * - 只提供与 SOEM 相关的“系统级查询”功能
     */
    class SOEM_INTERFACE_EXPORT SoemUtils
    {
    public:
        struct AdapterInfo
        {
            std::string name;   ///< 用于 ec_init 的接口名
            std::string desc;   ///< 人类可读描述
        };

        /**
         * @brief 扫描当前系统中可用的 EtherCAT 网络适配器
         *
         * @return AdapterInfo 列表（快照）
         */
        static std::vector<AdapterInfo> scanAdapters();
    };

namespace error {
    enum SoemInterfaceErrorCode
    {
        NoError,
        InvalidSlave,
        InvalidNicName,
        NoSlaveFound,
        EcatInitFailed,
        RequestOpFailed,
        RxPdoSizeMismatch,
        TxPdoSizeMismatch,
        EthercatNotOperational
    };
}
}
#endif // SOEMUTILS_H
