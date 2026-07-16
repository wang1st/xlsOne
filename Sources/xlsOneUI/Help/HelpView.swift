import SwiftUI
import xlsOneCore

// MARK: - Help Topic Model

/// A navigable section in the help window.
struct HelpTopic: Identifiable, Hashable {
    let id: String
    let icon: String
    let titleKey: String
    let blocks: [HelpBlock]

    var title: String { LocaleManager.loc(titleKey) }

    func matches(query: String) -> Bool {
        let normalized = query.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        guard !normalized.isEmpty else { return true }
        guard title.lowercased().contains(normalized) else {
            return blocks.contains { $0.searchableText.lowercased().contains(normalized) }
        }
        return true
    }
}

/// A single content block inside a help topic.
enum HelpBlock: Hashable {
    case heading(String)
    case paragraph(String)
    case numbered([String])
    case bullets([String])
    case tip(String)
    case note(String)
    case shortcuts([(key: String, action: String)])
    case contact(icon: String, label: String, value: String)

    var searchableText: String {
        switch self {
        case .heading(let key), .paragraph(let key), .tip(let key), .note(let key):
            return LocaleManager.loc(key)
        case .numbered(let keys), .bullets(let keys):
            return keys.map(LocaleManager.loc).joined(separator: " ")
        case .shortcuts(let pairs):
            return pairs.map { "\($0.key) \($0.action)" }.joined(separator: " ")
        case .contact(_, let labelKey, let valueKey):
            return "\(LocaleManager.loc(labelKey)) \(LocaleManager.loc(valueKey))"
        }
    }

    static func == (lhs: HelpBlock, rhs: HelpBlock) -> Bool {
        switch (lhs, rhs) {
        case (.heading(let a), .heading(let b)),
             (.paragraph(let a), .paragraph(let b)),
             (.tip(let a), .tip(let b)),
             (.note(let a), .note(let b)):
            return a == b
        case (.numbered(let a), .numbered(let b)),
             (.bullets(let a), .bullets(let b)):
            return a == b
        case (.shortcuts(let a), .shortcuts(let b)):
            return a.count == b.count && zip(a, b).allSatisfy { $0.key == $1.key && $0.action == $1.action }
        case (.contact(let i1, let l1, let v1), .contact(let i2, let l2, let v2)):
            return i1 == i2 && l1 == l2 && v1 == v2
        default:
            return false
        }
    }

    func hash(into hasher: inout Hasher) {
        switch self {
        case .heading(let s): hasher.combine(0); hasher.combine(s)
        case .paragraph(let s): hasher.combine(1); hasher.combine(s)
        case .numbered(let arr): hasher.combine(2); hasher.combine(arr)
        case .bullets(let arr): hasher.combine(3); hasher.combine(arr)
        case .tip(let s): hasher.combine(4); hasher.combine(s)
        case .note(let s): hasher.combine(5); hasher.combine(s)
        case .shortcuts(let pairs): hasher.combine(6); hasher.combine(pairs.map { $0.key + "\t" + $0.action })
        case .contact(let i, let l, let v): hasher.combine(7); hasher.combine(i); hasher.combine(l); hasher.combine(v)
        }
    }
}

// MARK: - Help Content

enum HelpContent {
    static let topics: [HelpTopic] = [
        quickStart,
        importFiles,
        viewingResults,
        sheetStatus,
        drillDown,
        exportResult,
        cellCorrection,
        schemaRules,
        keyboardShortcuts,
        faq,
        support
    ]

    private static let quickStart = HelpTopic(
        id: "quick-start",
        icon: "bolt",
        titleKey: "帮助_快速开始",
        blocks: [
            .paragraph("帮助_快速开始_介绍"),
            .heading("帮助_快速开始_步骤标题"),
            .numbered([
                "帮助_快速开始_步骤1",
                "帮助_快速开始_步骤2",
                "帮助_快速开始_步骤3",
                "帮助_快速开始_步骤4",
                "帮助_快速开始_步骤5"
            ]),
            .tip("帮助_快速开始_提示")
        ]
    )

    private static let importFiles = HelpTopic(
        id: "import-files",
        icon: "doc.badge.plus",
        titleKey: "帮助_导入文件",
        blocks: [
            .paragraph("帮助_导入文件_介绍"),
            .heading("帮助_导入文件_方式标题"),
            .bullets([
                "帮助_导入文件_方式1",
                "帮助_导入文件_方式2",
                "帮助_导入文件_方式3"
            ]),
            .heading("帮助_导入文件_追加标题"),
            .paragraph("帮助_导入文件_追加说明"),
            .note("帮助_导入文件_注意")
        ]
    )

    private static let viewingResults = HelpTopic(
        id: "viewing-results",
        icon: "tablecells",
        titleKey: "帮助_查看汇总结果",
        blocks: [
            .paragraph("帮助_查看汇总结果_介绍"),
            .heading("帮助_查看汇总结果_合并逻辑标题"),
            .bullets([
                "帮助_查看汇总结果_合并逻辑1",
                "帮助_查看汇总结果_合并逻辑2",
                "帮助_查看汇总结果_合并逻辑3"
            ]),
            .heading("帮助_查看汇总结果_穿透查阅标题"),
            .paragraph("帮助_查看汇总结果_穿透查阅说明"),
            .tip("帮助_查看汇总结果_提示")
        ]
    )

    private static let sheetStatus = HelpTopic(
        id: "sheet-status",
        icon: "checkmark.circle",
        titleKey: "帮助_工作表状态",
        blocks: [
            .paragraph("帮助_工作表状态_介绍"),
            .heading("帮助_工作表状态_可合并标题"),
            .paragraph("帮助_工作表状态_可合并说明"),
            .heading("帮助_工作表状态_已跳过标题"),
            .paragraph("帮助_工作表状态_已跳过说明"),
            .bullets([
                "帮助_工作表状态_跳过原因1",
                "帮助_工作表状态_跳过原因2",
                "帮助_工作表状态_跳过原因3"
            ]),
            .note("帮助_工作表状态_注意")
        ]
    )

    private static let drillDown = HelpTopic(
        id: "drilldown",
        icon: "magnifyingglass.circle",
        titleKey: "帮助_穿透查阅",
        blocks: [
            .paragraph("帮助_穿透查阅_介绍"),
            .heading("帮助_穿透查阅_步骤标题"),
            .numbered([
                "帮助_穿透查阅_步骤1",
                "帮助_穿透查阅_步骤2",
                "帮助_穿透查阅_步骤3"
            ]),
            .tip("帮助_穿透查阅_提示")
        ]
    )

    private static let exportResult = HelpTopic(
        id: "export",
        icon: "square.and.arrow.up",
        titleKey: "帮助_导出结果",
        blocks: [
            .paragraph("帮助_导出结果_介绍"),
            .heading("帮助_导出结果_步骤标题"),
            .numbered([
                "帮助_导出结果_步骤1",
                "帮助_导出结果_步骤2",
                "帮助_导出结果_步骤3",
                "帮助_导出结果_步骤4"
            ]),
            .heading("帮助_导出结果_内容说明标题"),
            .bullets([
                "帮助_导出结果_内容说明1",
                "帮助_导出结果_内容说明2",
                "帮助_导出结果_内容说明3",
                "帮助_导出结果_内容说明4"
            ])
        ]
    )

    private static let cellCorrection = HelpTopic(
        id: "cell-correction",
        icon: "pencil.circle",
        titleKey: "帮助_单元格修正",
        blocks: [
            .paragraph("帮助_单元格修正_介绍"),
            .heading("帮助_单元格修正_类型标题"),
            .bullets([
                "帮助_单元格修正_类型1",
                "帮助_单元格修正_类型2",
                "帮助_单元格修正_类型3"
            ]),
            .heading("帮助_单元格修正_操作标题"),
            .numbered([
                "帮助_单元格修正_操作1",
                "帮助_单元格修正_操作2",
                "帮助_单元格修正_操作3",
                "帮助_单元格修正_操作4"
            ]),
            .tip("帮助_单元格修正_提示")
        ]
    )

    private static let schemaRules = HelpTopic(
        id: "schema-rules",
        icon: "brain",
        titleKey: "帮助_修正规则",
        blocks: [
            .paragraph("帮助_修正规则_介绍"),
            .heading("帮助_修正规则_核心功能标题"),
            .bullets([
                "帮助_修正规则_自动匹配",
                "帮助_修正规则_保存规则",
                "帮助_修正规则_管理规则"
            ]),
            .heading("帮助_修正规则_最佳实践标题"),
            .bullets([
                "帮助_修正规则_最佳实践1",
                "帮助_修正规则_最佳实践2",
                "帮助_修正规则_最佳实践3"
            ]),
            .note("帮助_修正规则_技术说明")
        ]
    )

    private static let keyboardShortcuts = HelpTopic(
        id: "keyboard-shortcuts",
        icon: "keyboard",
        titleKey: "帮助_快捷键",
        blocks: [
            .paragraph("帮助_快捷键_介绍"),
            .shortcuts([
                (key: "⌘O", action: "帮助_快捷键_导入文件"),
                (key: "⇧⌘O", action: "帮助_快捷键_追加文件"),
                (key: "⌘S", action: "帮助_快捷键_导出"),
                (key: "⌘R", action: "帮助_快捷键_刷新"),
                (key: "⌘N", action: "帮助_快捷键_清空"),
                (key: "⌘Z", action: "帮助_快捷键_撤销"),
                (key: "1", action: "帮助_快捷键_标签"),
                (key: "2", action: "帮助_快捷键_求和"),
                (key: "3", action: "帮助_快捷键_单值"),
                (key: "J", action: "帮助_快捷键_下一个异常"),
                (key: "K", action: "帮助_快捷键_上一个异常"),
                (key: "⌘?", action: "帮助_快捷键_帮助")
            ])
        ]
    )

    private static let faq = HelpTopic(
        id: "faq",
        icon: "questionmark.bubble",
        titleKey: "帮助_常见问题",
        blocks: [
            .heading("帮助_常见问题_问题1"),
            .paragraph("帮助_常见问题_答案1"),
            .heading("帮助_常见问题_问题2"),
            .paragraph("帮助_常见问题_答案2"),
            .heading("帮助_常见问题_问题3"),
            .paragraph("帮助_常见问题_答案3"),
            .heading("帮助_常见问题_问题4"),
            .paragraph("帮助_常见问题_答案4")
        ]
    )

    private static let support = HelpTopic(
        id: "support",
        icon: "envelope",
        titleKey: "帮助_联系方式",
        blocks: [
            .paragraph("帮助_联系方式_介绍"),
            .contact(icon: "envelope", label: "帮助_联系方式_邮箱标签", value: "帮助_联系方式_邮箱值")
        ]
    )
}

// MARK: - Help View

/// A professional, searchable help window for xlsOne.
public struct HelpView: View {
    @ObservedObject private var localeManager = LocaleManager.shared
    @State private var selectedTopicID: String?
    @State private var searchQuery: String = ""

    public init() {}

    private var topics: [HelpTopic] { HelpContent.topics }

    private var filteredTopics: [HelpTopic] {
        topics.filter { $0.matches(query: searchQuery) }
    }

    private var selectedTopic: HelpTopic? {
        guard let selectedTopicID else { return topics.first }
        return topics.first { $0.id == selectedTopicID } ?? topics.first
    }

    public var body: some View {
        HStack(spacing: 0) {
            sidebar
            Divider()
            contentArea
        }
        .frame(minWidth: 900, idealWidth: 1000, maxWidth: .infinity, minHeight: 640, idealHeight: 700, maxHeight: .infinity)
        .background(XColor.background)
        .id(localeManager.currentLanguage.rawValue)
    }

    // MARK: - Sidebar

    private var sidebar: some View {
        VStack(spacing: 0) {
            searchField
                .padding(XSpacing.md)

            ScrollViewReader { proxy in
                List(filteredTopics) { topic in
                    sidebarItem(for: topic)
                        .id(topic.id)
                }
                .listStyle(.plain)
                .onChange(of: selectedTopicID) { _ in
                    if let id = selectedTopicID {
                        withAnimation {
                            proxy.scrollTo(id, anchor: .center)
                        }
                    }
                }
            }
        }
        .frame(width: 240)
        .background(XColor.surface)
    }

    private var searchField: some View {
        HStack(spacing: XSpacing.sm) {
            Image(systemName: "magnifyingglass")
                .font(XFont.caption)
                .foregroundColor(XColor.secondaryLabel)

            TextField(LocaleManager.loc("帮助_搜索占位符"), text: $searchQuery)
                .font(XFont.body)
                .textFieldStyle(.plain)

            if !searchQuery.isEmpty {
                Button {
                    searchQuery = ""
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .font(XFont.caption)
                        .foregroundColor(XColor.secondaryLabel)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, XSpacing.sm)
        .padding(.vertical, XSpacing.xs)
        .background(XColor.background)
        .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: XRadius.md, style: .continuous)
                .stroke(XColor.border, lineWidth: 1)
        )
    }

    private func sidebarItem(for topic: HelpTopic) -> some View {
        Button {
            selectedTopicID = topic.id
        } label: {
            HStack(spacing: XSpacing.sm) {
                Image(systemName: topic.icon)
                    .font(XFont.body)
                    .foregroundColor(isSelected(topic) ? XColor.accent : XColor.secondaryLabel)
                    .frame(width: 22)

                Text(topic.title)
                    .font(XFont.body)
                    .foregroundColor(isSelected(topic) ? XColor.primaryLabel : XColor.secondaryLabel)
                    .lineLimit(1)

                Spacer()
            }
            .padding(.vertical, XSpacing.sm)
            .padding(.horizontal, XSpacing.md)
            .background(isSelected(topic) ? XColor.selectedCellBackground : Color.clear)
            .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))
        }
        .buttonStyle(.plain)
    }

    private func isSelected(_ topic: HelpTopic) -> Bool {
        selectedTopic?.id == topic.id
    }

    // MARK: - Content Area

    private var contentArea: some View {
        Group {
            if let topic = selectedTopic {
                ScrollView {
                    VStack(alignment: .leading, spacing: XSpacing.lg) {
                        topicHeader(for: topic)
                        ForEach(Array(topic.blocks.enumerated()), id: \.0) { _, block in
                            blockView(block)
                        }
                        Spacer(minLength: XSpacing.xxl)
                    }
                    .padding(XSpacing.xl)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            } else {
                emptyContentView
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var emptyContentView: some View {
        VStack(spacing: XSpacing.md) {
            Image(systemName: "book")
                .font(.system(size: 40))
                .foregroundColor(XColor.secondaryLabel)
            Text(LocaleManager.loc("帮助_选择章节"))
                .font(XFont.sectionTitle)
                .foregroundColor(XColor.secondaryLabel)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func topicHeader(for topic: HelpTopic) -> some View {
        HStack(spacing: XSpacing.md) {
            Image(systemName: topic.icon)
                .font(.system(size: 28, weight: .medium))
                .foregroundColor(XColor.accent)

            VStack(alignment: .leading, spacing: XSpacing.xs) {
                Text(topic.title)
                    .font(XFont.windowTitle)
                    .foregroundColor(XColor.primaryLabel)

                Text(LocaleManager.loc("帮助_章节副标题"))
                    .font(XFont.callout)
                    .foregroundColor(XColor.secondaryLabel)
            }
        }
        .padding(.bottom, XSpacing.md)
    }

    // MARK: - Block Views

    @ViewBuilder
    private func blockView(_ block: HelpBlock) -> some View {
        switch block {
        case .heading(let key):
            Text(LocaleManager.loc(key))
                .font(XFont.sectionTitle)
                .foregroundColor(XColor.primaryLabel)
                .padding(.top, XSpacing.sm)

        case .paragraph(let key):
            Text(LocaleManager.loc(key))
                .font(XFont.body)
                .foregroundColor(XColor.secondaryLabel)
                .lineSpacing(4)
                .fixedSize(horizontal: false, vertical: true)

        case .numbered(let keys):
            VStack(alignment: .leading, spacing: XSpacing.sm) {
                ForEach(Array(keys.enumerated()), id: \.0) { index, key in
                    HStack(alignment: .top, spacing: XSpacing.sm) {
                        Text("\(index + 1)")
                            .font(XFont.caption)
                            .fontWeight(.semibold)
                            .foregroundColor(.white)
                            .frame(width: 20, height: 20)
                            .background(XColor.accent)
                            .clipShape(Circle())

                        Text(LocaleManager.loc(key))
                            .font(XFont.body)
                            .foregroundColor(XColor.secondaryLabel)
                            .lineSpacing(4)
                            .fixedSize(horizontal: false, vertical: true)

                        Spacer(minLength: 0)
                    }
                }
            }

        case .bullets(let keys):
            VStack(alignment: .leading, spacing: XSpacing.sm) {
                ForEach(keys, id: \.self) { key in
                    HStack(alignment: .top, spacing: XSpacing.sm) {
                        Circle()
                            .fill(XColor.accent)
                            .frame(width: 6, height: 6)
                            .padding(.top, 6)

                        Text(LocaleManager.loc(key))
                            .font(XFont.body)
                            .foregroundColor(XColor.secondaryLabel)
                            .lineSpacing(4)
                            .fixedSize(horizontal: false, vertical: true)

                        Spacer(minLength: 0)
                    }
                }
            }

        case .tip(let key):
            calloutBlock(
                icon: "lightbulb.fill",
                color: XColor.warning,
                text: LocaleManager.loc(key)
            )

        case .note(let key):
            calloutBlock(
                icon: "info.circle.fill",
                color: XColor.info,
                text: LocaleManager.loc(key)
            )

        case .shortcuts(let pairs):
            VStack(alignment: .leading, spacing: XSpacing.sm) {
                ForEach(Array(pairs.enumerated()), id: \.0) { _, pair in
                    HStack(spacing: XSpacing.md) {
                        Text(pair.key)
                            .font(XFont.monospacedData)
                            .foregroundColor(XColor.primaryLabel)
                            .padding(.horizontal, XSpacing.sm)
                            .padding(.vertical, XSpacing.xs)
                            .background(XColor.surface)
                            .clipShape(RoundedRectangle(cornerRadius: XRadius.sm, style: .continuous))
                            .overlay(
                                RoundedRectangle(cornerRadius: XRadius.sm, style: .continuous)
                                    .stroke(XColor.border, lineWidth: 1)
                            )

                        Text(LocaleManager.loc(pair.action))
                            .font(XFont.body)
                            .foregroundColor(XColor.secondaryLabel)

                        Spacer()
                    }
                }
            }
            .padding(XSpacing.md)
            .background(XColor.surface)
            .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))

        case .contact(let icon, let labelKey, let valueKey):
            let value = LocaleManager.loc(valueKey)
            let isEmail = value.contains("@")
            let url = isEmail ? URL(string: "mailto:\(value)") : URL(string: "https://\(value)")

            HStack(spacing: XSpacing.md) {
                Image(systemName: icon)
                    .font(XFont.body)
                    .foregroundColor(XColor.accent)
                    .frame(width: 24)

                VStack(alignment: .leading, spacing: XSpacing.xs) {
                    Text(LocaleManager.loc(labelKey))
                        .font(XFont.caption)
                        .foregroundColor(XColor.tertiaryLabel)
                    if let url {
                        Link(value, destination: url)
                            .font(XFont.body)
                            .foregroundColor(XColor.accent)
                    } else {
                        Text(value)
                            .font(XFont.body)
                            .foregroundColor(XColor.primaryLabel)
                            .textSelection(.enabled)
                    }
                }

                Spacer()
            }
            .padding(XSpacing.md)
            .background(XColor.surface)
            .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))
        }
    }

    private func calloutBlock(icon: String, color: Color, text: String) -> some View {
        HStack(alignment: .top, spacing: XSpacing.sm) {
            Image(systemName: icon)
                .font(XFont.callout)
                .foregroundColor(color)
                .padding(.top, 1)

            Text(text)
                .font(XFont.body)
                .foregroundColor(XColor.secondaryLabel)
                .lineSpacing(4)
                .fixedSize(horizontal: false, vertical: true)

            Spacer(minLength: 0)
        }
        .padding(XSpacing.md)
        .background(color.opacity(0.08))
        .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: XRadius.md, style: .continuous)
                .stroke(color.opacity(0.18), lineWidth: 1)
        )
    }
}

#if DEBUG
struct HelpView_Previews: PreviewProvider {
    static var previews: some View {
        HelpView()
    }
}
#endif
