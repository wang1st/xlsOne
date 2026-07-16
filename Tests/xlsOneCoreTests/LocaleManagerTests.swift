import XCTest
@testable import xlsOneCore

final class LocaleManagerTests: XCTestCase {
    func testHelpKeysAreLocalized() {
        let keys = [
            "帮助_快速开始",
            "帮助_快速开始_介绍",
            "帮助_导入文件",
            "帮助_查看汇总结果",
            "帮助_修正规则",
            "帮助_联系方式",
            "帮助_联系方式_邮箱值",
            "帮助_搜索占位符",
            "快速参考指南"
        ]

        for key in keys {
            let zh = LocaleManager.loc(key, for: .chineseSimplified)
            XCTAssertFalse(zh.isEmpty, "Localized value for '\(key)' is empty in zh-Hans")

            let en = LocaleManager.loc(key, for: .english)
            XCTAssertFalse(en.isEmpty, "Localized value for '\(key)' is empty in en")
        }
    }

    func testCurrentLanguageReturnsChineseForSimplified() {
        let value = LocaleManager.loc("帮助_快速开始", for: .chineseSimplified)
        XCTAssertEqual(value, "快速开始")
    }

    func testCurrentLanguageReturnsEnglish() {
        let value = LocaleManager.loc("帮助_快速开始", for: .english)
        XCTAssertEqual(value, "Quick Start")
    }
}
