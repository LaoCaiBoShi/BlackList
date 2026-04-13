#ifndef ZIP_BASIC_H
#define ZIP_BASIC_H

#include <string>
#include <vector>

/**
 * @brief 基本解压缩功能接口
 */
class ZipBasic {
public:
    /**
     * @brief 解压ZIP文件到指定目录
     * @param zipPath ZIP文件路径
     * @param destDir 目标目录
     * @return 解压是否成功
     */
    static bool extractZip(const std::string& zipPath, const std::string& destDir);
    
    /**
     * @brief 获取ZIP文件中的文件列表
     * @param zipPath ZIP文件路径
     * @param fileList 输出参数，存储文件列表
     * @return 获取是否成功
     */
    static bool getZipFileList(const std::string& zipPath, std::vector<std::string>& fileList);
    
    /**
     * @brief 从ZIP文件中提取指定文件
     * @param zipPath ZIP文件路径
     * @param fileName ZIP中的文件名
     * @param destPath 目标文件路径
     * @return 提取是否成功
     */
    static bool extractFileFromZip(const std::string& zipPath, const std::string& fileName, const std::string& destPath);
};

#endif // ZIP_BASIC_H
