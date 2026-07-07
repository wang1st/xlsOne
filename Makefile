# xlsOne 本地质量门禁快捷指令
# 用法：make format / make lint / make test / make ci

.PHONY: format lint test ci

format:
	swiftformat Sources Tests

lint:
	swiftlint lint

test:
	swift test

ci: lint test
