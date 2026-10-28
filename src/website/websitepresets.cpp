#include "website/websitepresets.h"
#include "distributionprofile.h"

#include <QJsonArray>

QList<PresetWebsite> builtInPresetWebsites()
{
    const QJsonArray configured = distributionProfile().value(QStringLiteral("home_sites")).toArray();
    if (!configured.isEmpty()) {
        QList<PresetWebsite> websites;
        for (const QJsonValue &value : configured) {
            const QJsonObject item = value.toObject();
            const QString name = item.value(QStringLiteral("name")).toString();
            const QString url = item.value(QStringLiteral("url")).toString();
            if (!name.isEmpty() && !url.isEmpty()) {
                websites.append({name, url, item.value(QStringLiteral("icon")).toString()});
            }
        }
        return websites;
    }
    return {
        {QStringLiteral("哔哩哔哩"),
         QStringLiteral("https://www.bilibili.com"),
         QStringLiteral("bilibili.svg")},
        {QStringLiteral("优酷"),
         QStringLiteral("https://www.youku.com"),
         QStringLiteral("youku.svg")},
        {QStringLiteral("爱奇艺"),
         QStringLiteral("https://www.iqiyi.com"),
         QStringLiteral("iqiyi.svg")},
        {QStringLiteral("腾讯视频"),
         QStringLiteral("https://v.qq.com"),
         QStringLiteral("tencent-video.svg")},
        {QStringLiteral("芒果TV"),
         QStringLiteral("https://www.mgtv.com"),
         QStringLiteral("mango.svg")},
        {QStringLiteral("咪咕视频"),
         QStringLiteral("https://www.miguvideo.com"),
         QStringLiteral("migu.svg")},
        {QStringLiteral("1905 电影网"),
         QStringLiteral("https://www.1905.com"),
         QStringLiteral("1905dianying.svg")},
        {QStringLiteral("抖音"),
         QStringLiteral("https://www.douyin.com"),
         QStringLiteral("douyin.svg")},
        {QStringLiteral("好看视频"),
         QStringLiteral("https://haokan.baidu.com"),
         QStringLiteral("haokan.svg")},
        {QStringLiteral("红果短剧"),
         QStringLiteral("https://www.hongguoduanju.com"),
         QStringLiteral("hongguo.png")},
        {QStringLiteral("快手"),
         QStringLiteral("https://www.kuaishou.com"),
         QStringLiteral("kuaishou.svg")},
        {QStringLiteral("虎牙直播"),
         QStringLiteral("https://www.huya.com"),
         QStringLiteral("huya.svg")},
        {QStringLiteral("搜狐视频"),
         QStringLiteral("https://tv.sohu.com"),
         QStringLiteral("sohu.svg")},
    };
}
