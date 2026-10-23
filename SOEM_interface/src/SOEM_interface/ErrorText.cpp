#include "SOEM_interface/ErrorText.h"

#include <iomanip>
#include <sstream>

namespace soem_interface {
namespace {

std::string unknownCode(const char* description, uint32_t code, int width)
{
    std::ostringstream text;
    text << description << "（0x" << std::uppercase << std::hex
         << std::setfill('0') << std::setw(width) << code << "）";
    return text.str();
}

} // namespace

// Keep these translations aligned with the local SOEM tables, including their
// existing wording. Do not infer meanings for codes absent from those tables.
std::string alStatusText(uint16_t code)
{
    switch (code) {
    case 0x0000: return "无错误";
    case 0x0001: return "未指定的错误";
    case 0x0002: return "内存不足";
    case 0x0003: return "设备设置无效";
    case 0x0004: return "修订版本无效";
    case 0x0006: return "SII/EEPROM 信息与固件不匹配";
    case 0x0007: return "固件更新失败，旧固件仍在运行";
    case 0x000E: return "许可证错误";
    case 0x0011: return "请求的状态切换无效";
    case 0x0012: return "请求的状态未知";
    case 0x0013: return "不支持 Bootstrap";
    case 0x0014: return "没有有效固件";
    case 0x0015: return "邮箱配置无效";
    case 0x0016: return "邮箱配置无效";
    case 0x0017: return "同步管理器（SM）配置无效";
    case 0x0018: return "没有可用的有效输入";
    case 0x0019: return "没有有效输出";
    case 0x001A: return "同步错误";
    case 0x001B: return "同步管理器（SM）看门狗触发";
    case 0x001C: return "同步管理器（SM）类型无效";
    case 0x001D: return "输出配置无效";
    case 0x001E: return "输入配置无效";
    case 0x001F: return "看门狗配置无效";
    case 0x0020: return "从站需要冷启动";
    case 0x0021: return "从站需要进入 INIT";
    case 0x0022: return "从站需要进入 PRE-OP";
    case 0x0023: return "从站需要进入 SAFE-OP";
    case 0x0024: return "输入映射无效";
    case 0x0025: return "输出映射无效";
    case 0x0026: return "设置不一致";
    case 0x0027: return "不支持自由运行模式";
    case 0x0028: return "不支持同步模式";
    case 0x0029: return "自由运行需要三缓冲模式";
    case 0x002A: return "后台看门狗触发";
    case 0x002B: return "没有有效输入和输出";
    case 0x002C: return "严重同步错误";
    case 0x002D: return "无同步错误";
    case 0x002E: return "输入 FMMU 配置无效";
    case 0x0030: return "DC SYNC 配置无效";
    case 0x0031: return "DC 锁存配置无效";
    case 0x0032: return "PLL 错误";
    case 0x0033: return "DC 同步 IO 错误";
    case 0x0034: return "DC 同步超时";
    case 0x0035: return "DC 同步周期无效";
    case 0x0036: return "DC SYNC0 周期无效";
    case 0x0037: return "DC SYNC1 周期无效";
    case 0x0041: return "邮箱错误（MBX_AOE）";
    case 0x0042: return "邮箱错误（MBX_EOE）";
    case 0x0043: return "邮箱错误（MBX_COE）";
    case 0x0044: return "邮箱错误（MBX_FOE）";
    case 0x0045: return "邮箱错误（MBX_SOE）";
    case 0x004F: return "邮箱错误（MBX_VOE）";
    case 0x0050: return "无法访问 EEPROM";
    case 0x0051: return "EEPROM 错误";
    case 0x0052: return "外部硬件未就绪";
    case 0x0060: return "从站已在本地重启";
    case 0x0061: return "设备标识值已更新";
    case 0x0070: return "检测到的模块标识列表不匹配";
    case 0x0080: return "供电电压过低";
    case 0x0081: return "供电电压过高";
    case 0x0082: return "温度过低";
    case 0x0083: return "温度过高";
    case 0x00F0: return "应用控制器可用";
    default: return unknownCode("未知 AL 状态码", code, 4);
    }
}

std::string sdoAbortText(uint32_t code)
{
    switch (code) {
    case 0x00000000: return "无错误";
    case 0x05030000: return "切换位（Toggle bit）未改变";
    case 0x05040000: return "SDO 协议超时";
    case 0x05040001: return "客户端/服务器命令标识无效或未知";
    case 0x05040005: return "内存不足";
    case 0x06010000: return "不支持此对象访问方式";
    case 0x06010001: return "尝试读取只写对象";
    case 0x06010002: return "尝试写入只读对象";
    case 0x06010003: return "子索引不可写入，写入时 SI0 必须为 0";
    case 0x06010004: return "可变长度对象不支持 SDO 完整访问（Complete Access）";
    case 0x06010005: return "对象长度超出邮箱大小";
    case 0x06010006: return "对象已映射到 RxPDO，SDO 下载被阻止";
    case 0x06020000: return "对象字典中不存在此对象";
    case 0x06040041: return "此对象不能映射到 PDO";
    case 0x06040042: return "待映射对象的数量和长度超出 PDO 长度";
    case 0x06040043: return "一般参数不兼容";
    case 0x06040047: return "设备内部存在一般不兼容问题";
    case 0x06060000: return "硬件错误导致访问失败";
    case 0x06070010: return "数据类型不匹配，服务参数长度不匹配";
    case 0x06070012: return "数据类型不匹配，服务参数长度过长";
    case 0x06070013: return "数据类型不匹配，服务参数长度过短";
    case 0x06090011: return "子索引不存在";
    case 0x06090030: return "写入参数超出取值范围";
    case 0x06090031: return "写入参数值过大";
    case 0x06090032: return "写入参数值过小";
    case 0x06090036: return "最大值小于最小值";
    case 0x08000000: return "一般错误";
    case 0x08000020: return "数据无法传输或保存到应用程序";
    case 0x08000021: return "本地控制导致数据无法传输或保存到应用程序";
    case 0x08000022: return "当前设备状态导致数据无法传输或保存到应用程序";
    case 0x08000023: return "对象字典动态生成失败，或对象字典不存在";
    default: return unknownCode("未知 SDO 中止码", code, 8);
    }
}

std::string soeErrorText(uint16_t code)
{
    switch (code) {
    case 0x0000: return "无错误";
    case 0x1001: return "没有 IDN";
    case 0x1009: return "对元素 1 的访问无效";
    case 0x2001: return "没有名称";
    case 0x2002: return "名称传输长度过短";
    case 0x2003: return "名称传输长度过长";
    case 0x2004: return "名称不能修改（只读）";
    case 0x2005: return "名称当前处于写保护状态";
    case 0x3002: return "属性传输长度过短";
    case 0x3003: return "属性传输长度过长";
    case 0x3004: return "属性不能修改（只读）";
    case 0x3005: return "属性当前处于写保护状态";
    case 0x4001: return "没有单位";
    case 0x4002: return "单位传输长度过短";
    case 0x4003: return "单位传输长度过长";
    case 0x4004: return "单位不能修改（只读）";
    case 0x4005: return "单位当前处于写保护状态";
    case 0x5001: return "没有最小输入值";
    case 0x5002: return "最小输入值传输长度过短";
    case 0x5003: return "最小输入值传输长度过长";
    case 0x5004: return "最小输入值不能修改（只读）";
    case 0x5005: return "最小输入值当前处于写保护状态";
    case 0x6001: return "没有最大输入值";
    case 0x6002: return "最大输入值传输长度过短";
    case 0x6003: return "最大输入值传输长度过长";
    case 0x6004: return "最大输入值不能修改（只读）";
    case 0x6005: return "最大输入值当前处于写保护状态";
    case 0x7002: return "运行数据传输长度过短";
    case 0x7003: return "运行数据传输长度过长";
    case 0x7004: return "运行数据不能修改（只读）";
    case 0x7005: return "当前状态下运行数据处于写保护状态";
    case 0x7006: return "运行数据小于最小输入值";
    // The local SOEM table says "smaller than the maximum input value".
    case 0x7007: return "运行数据小于最大输入值";
    case 0x7008: return "运行数据无效：不支持配置的 IDN";
    case 0x7009: return "运行数据受密码写保护";
    case 0x700A: return "运行数据已配置为周期传输，处于写保护状态";
    case 0x700B: return "间接寻址无效（例如数据容器或列表处理）";
    case 0x700C: return "其他设置导致运行数据处于写保护状态";
    case 0x700D: return "保留错误码";
    case 0x7010: return "过程命令已在执行";
    case 0x7011: return "过程命令不可中断";
    case 0x7012: return "当前状态下无法执行过程命令";
    case 0x7013: return "过程命令无法执行（参数无效或错误）";
    case 0x7014: return "没有数据状态";
    case 0x8001: return "没有默认值";
    case 0x8002: return "默认值传输长度过长";
    case 0x8004: return "默认值不能修改（只读）";
    // SOEM lists 0x800A three times; its lookup returns the first entry.
    case 0x800A: return "驱动器编号无效";
    default: return unknownCode("未知 SoE 错误码", code, 4);
    }
}

std::string mailboxErrorText(uint16_t code)
{
    switch (code) {
    case 0x0000: return "无错误";
    case 0x0001: return "6 字节邮箱报头的语法错误";
    case 0x0002: return "不支持此邮箱协议";
    case 0x0003: return "通道字段的值错误";
    case 0x0004: return "不支持此服务";
    case 0x0005: return "邮箱报头无效";
    case 0x0006: return "接收到的邮箱数据长度过短";
    case 0x0007: return "从站内存不足";
    case 0x0008: return "数据长度不一致";
    default: return unknownCode("未知邮箱错误码", code, 4);
    }
}

} // namespace soem_interface
