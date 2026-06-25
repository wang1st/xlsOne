import Foundation

public final class AlgorithmI18n {
    public static let shared = AlgorithmI18n()

    public struct AlgorithmKeywords {
        public let amountPatterns: [String]
        public let weakAmountPatterns: [String]
        public let codePatterns: [String]
        public let labelPatterns: [String]
        public let sumForcingPatterns: [String]
        public let currencySymbols: [String]
        public let dollarSymbols: [String]
        public let dashMarkers: [String]
        public let strongAmountKeywords: [String]
        public let mediumAmountKeywords: [String]
        public let strongLabelKeywords: [String]
        public let strongCodeKeywords: [String]
        public let strongDateKeywords: [String]
        public let amountKeywordsEn: [String]
        public let codeKeywordsEn: [String]
        public let labelKeywordsEn: [String]
    }

    private var _current: AlgorithmKeywords

    private init() {
        _current = AlgorithmI18n.loadMerged()
    }

    public func reload() {
        _current = AlgorithmI18n.loadMerged()
    }

    public var current: AlgorithmKeywords { _current }

    /// Load patterns from all available languages and merge them.
    /// This ensures the algorithm works for both Chinese and English spreadsheets simultaneously.
    private static func loadMerged() -> AlgorithmKeywords {
        guard let url = Bundle.module.url(forResource: "AlgorithmKeywords", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else {
            return builtinMerged()
        }

        func mergedStrings(_ key: String) -> [String] {
            var all: Set<String> = []
            for (_, langData) in json {
                if let dict = langData as? [String: Any], let arr = dict[key] as? [String] {
                    all.formUnion(arr)
                }
            }
            return Array(all)
        }

        return AlgorithmKeywords(
            amountPatterns: mergedStrings("amountPatterns"),
            weakAmountPatterns: mergedStrings("weakAmountPatterns"),
            codePatterns: mergedStrings("codePatterns"),
            labelPatterns: mergedStrings("labelPatterns"),
            sumForcingPatterns: mergedStrings("sumForcingPatterns"),
            currencySymbols: mergedStrings("currencySymbols"),
            dollarSymbols: mergedStrings("dollarSymbols"),
            dashMarkers: mergedStrings("dashMarkers"),
            strongAmountKeywords: mergedStrings("strongAmountKeywords"),
            mediumAmountKeywords: mergedStrings("mediumAmountKeywords"),
            strongLabelKeywords: mergedStrings("strongLabelKeywords"),
            strongCodeKeywords: mergedStrings("strongCodeKeywords"),
            strongDateKeywords: mergedStrings("strongDateKeywords"),
            amountKeywordsEn: mergedStrings("amountKeywordsEn"),
            codeKeywordsEn: mergedStrings("codeKeywordsEn"),
            labelKeywordsEn: mergedStrings("labelKeywordsEn")
        )
    }

    private static func builtinMerged() -> AlgorithmKeywords {
        AlgorithmKeywords(
            amountPatterns: ["合计","总计","小计","金额","数额","额度","数量","单价","总价","价格","数值","预算","收入","支出","成本","费用","利润","执行","决算","款","税金","人数","人口","户数","家数","个数","人员","编制","职工","sum","total","subtotal","amount","quantity","qty","price","unit price","total price","value","budget","revenue","income","expense","cost","fee","profit","tax","fund","population","headcount","staff"],
            weakAmountPatterns: ["数","额","值","量","价","count","val","qty","amt"],
            codePatterns: ["代码","编码","编号","序号","号码","证号","区划","邮编","邮政编码","身份证","电话","传真","期间","年月","年份","日期","时间","学号","工号","账号","户号","卡号","单号","订单号","票号","发票号","批号","书号","卷号","册号","期号","版号","件号","条码","档案号","准考证号","资格证号","许可证号","机号","箱号","包号","袋号","code","number","no.","no "," id","index","serial","zip","zipcode","postal code","phone","tel","fax","period","date","time","year","month"],
            labelPatterns: ["名称","名字","描述","说明","备注","标题","内容","详情","类型","性质","状态","name","desc","description","title","remark","note","type","kind","status","content","detail"],
            sumForcingPatterns: ["合计","总计","小计","sum","total","subtotal","grand total"],
            currencySymbols: ["¥","\\¥","[$¥]"],
            dollarSymbols: ["$","[$-"],
            dashMarkers: ["—","-","/","NA","N/A","无","null","NULL","~","none","n/a"],
            strongAmountKeywords: ["金额","总额","合计","总计","小计","预算","执行","决算","收入","支出","费用","成本","资金","付款","收款","amount","total","sum","subtotal","budget","revenue","income","expense","cost","fee","fund","payment","tax"],
            mediumAmountKeywords: ["数","额","值","量","价","count","val","qty","amt","rate"],
            strongLabelKeywords: ["名称","名字","描述","说明","备注","标题","内容","详情","注释","name","description","remark","note","title","comment","detail"],
            strongCodeKeywords: ["代码","编码","编号","序号","id","code","no","区划","行政区划","科目代码","项目代码","邮编","电话","传真","社会信用代码","统一代码","number","index","serial","zip","zipcode","postal code","phone","tel","fax"],
            strongDateKeywords: ["日期","时间","年度","年份","月份","年月","填报日期","报送日期","截止日期","创建时间","date","time","year","month","period","created","updated"],
            amountKeywordsEn: ["sum","total","subtotal","amount","quantity","qty","price","unit price","total price","value","budget","revenue","income","expense","cost","fee","profit","tax","fund","population","headcount","staff"],
            codeKeywordsEn: ["code","number","no.","no "," id","index","serial","zip","zipcode","postal code","phone","tel","fax","period","date","time","year","month"],
            labelKeywordsEn: ["name","desc","description","title","remark","note","type","kind","status","content","detail"]
        )
    }
}
