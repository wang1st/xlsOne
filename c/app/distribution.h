#ifndef XLSONE_DISTRIBUTION_H
#define XLSONE_DISTRIBUTION_H

/* Keep each distribution's purchases, licenses and updates on the same service. */
#ifdef XLSONE_INTERNATIONAL
#define XLSONE_ACTIVATION_BASE_URL "https://api.xlsone.com"
#define XLSONE_UPDATE_URL "https://xlsone.com/api/version.json"
#define XLSONE_WEBSITE_URL "https://xlsone.com/"
#define XLSONE_SUPPORT_URL "https://xlsone.com/support/"
#define XLSONE_PURCHASE_URL "https://xlsone.com/pricing/"
#define XLSONE_OFFLINE_URL "https://xlsone.com/support/#offline"
#define XLSONE_DOWNLOAD_URL "https://xlsone.com/download/"
#else
#define XLSONE_ACTIVATION_BASE_URL "https://z-pulse.cn"
#define XLSONE_UPDATE_URL "https://z-pulse.cn/api/version"
#define XLSONE_WEBSITE_URL "https://z-pulse.cn/xlsone/"
#define XLSONE_SUPPORT_URL "https://z-pulse.cn/xlsone/"
#define XLSONE_PURCHASE_URL "https://z-pulse.cn/xlsone/buy.html"
#define XLSONE_OFFLINE_URL "https://z-pulse.cn/xlsone/offline"
#define XLSONE_DOWNLOAD_URL "https://z-pulse.cn/products/xlsone/download.html"
#endif

#endif
