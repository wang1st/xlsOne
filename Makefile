# xlsOne 纯 C 版快捷指令

.PHONY: configure build test run package verify-no-qt ci swift-format swift-lint swift-test

configure:
	cmake -S . -B build -G Ninja -DXLSONE_C_WARNINGS_AS_ERRORS=ON

build: configure
	cmake --build build

test: build
	ctest --test-dir build --output-on-failure

run: build
	open build/c/app/xlsOne.app

package: test
	cmake --build build --target package

verify-no-qt: build
	c/scripts/verify_no_qt.sh build/c/app/xlsOne.app/Contents/MacOS/xlsOne

ci: test verify-no-qt

swift-format:
	swiftformat Sources Tests

swift-lint:
	swiftlint lint

swift-test:
	swift test
