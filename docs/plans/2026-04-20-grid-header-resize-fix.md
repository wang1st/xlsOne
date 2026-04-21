# Excel Grid Header Alignment And Resize Cursor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix the Excel-like grid so the sticky column header stays pixel-aligned with body cells and the column-resize divider reliably shows the horizontal resize cursor.

**Architecture:** Keep the current SwiftUI/AppKit macOS stack, but stop relying on ad hoc duplicated layout code inside `ExcelGridView`. Introduce one shared column-metrics contract for header cells, body cells, and resize handles, then cover the new behavior with pure unit tests plus macOS-hosted layout tests.

**Tech Stack:** SwiftUI, AppKit (`NSCursor`, `NSHostingView`), XCTest

---

## Findings

1. The sticky header and the scrollable grid body do not share a single reusable column layout primitive today. Header widths are applied in `columnHeaders`, while body widths are applied separately in `ExcelCellView`, each with their own modifier order and padding behavior. Relevant code: `Sources/xlsOne/ContentView.swift:438-449`, `Sources/xlsOne/ContentView.swift:593-652`, `Sources/xlsOne/ContentView.swift:878-899`.

2. The grid currently renders the column header twice: once inside the scroll view and once again as a fixed overlay. Relevant code: `Sources/xlsOne/ContentView.swift:333-335`, `Sources/xlsOne/ContentView.swift:376-381`. That makes alignment bugs harder to reason about and increases the chance that hover/gesture behavior is attached to the wrong visible layer.

3. The resize handle is only `4pt` wide and uses `NSCursor.set()` inside `onHover`. Relevant code: `Sources/xlsOne/ContentView.swift:618-648`. On macOS this is fragile because AppKit cursor rect updates can overwrite `set()`, and a `4pt` invisible hit target is too small for reliable hover feedback.

4. Width computation and rendered width use different semantics. `initializeColumnWidths()` adds text padding into the stored width, then both header and body add extra horizontal padding again during rendering. Relevant code: `Sources/xlsOne/ContentView.swift:452-495`, `Sources/xlsOne/ContentView.swift:604-605`, `Sources/xlsOne/ContentView.swift:883-884`. Even when the UI looks close, this is not a stable contract.

## Recommended Approach

Use a focused refactor instead of a wholesale grid rewrite.

- Keep the current `ExcelGridView` behavior, scroll model, and sticky overlays.
- Remove the duplicated interactive header from inside the scroll content.
- Introduce one shared column frame model that defines:
  - content width
  - outer rendered width
  - horizontal padding
  - row/header height
  - minimum resize width
- Make header cells, body cells, and resize handles all consume that same model.
- Replace direct `NSCursor.set()` calls with a dedicated cursor-management helper that uses cursor stack semantics (`push`/`pop`) or an AppKit-backed tracking view if SwiftUI hover remains unreliable.

This is the lowest-risk path because it fixes the misalignment and cursor bug without rewriting the whole grid or touching merge logic.

## Non-Goals

- Do not change merge logic in `xlsOneCore`.
- Do not redesign the overall visual style of the app.
- Do not introduce virtualization in this pass.
- Do not add a separate Xcode project solely for UI tests unless the existing SwiftPM test target proves insufficient.

### Task 1: Extract Shared Grid Metrics

**Files:**
- Create: `Sources/xlsOne/Grid/GridMetrics.swift`
- Modify: `Sources/xlsOne/ContentView.swift`
- Test: `Tests/xlsOneTests/GridMetricsTests.swift`

**Step 1: Write the failing test**

Add tests for a new `GridMetrics` or `ColumnWidthCalculator` type:

```swift
func testRenderedWidthMatchesContentWidthPlusInsets() {
    let metrics = GridMetrics(contentWidth: 96, horizontalInset: 4)
    XCTAssertEqual(metrics.renderedWidth, 104)
}

func testResizedWidthClampsToMinimum() {
    XCTAssertEqual(GridMetrics.clampedWidth(start: 80, translation: -100), 40)
}
```

**Step 2: Run test to verify it fails**

Run: `swift test --filter GridMetricsTests`
Expected: FAIL because `GridMetrics` does not exist yet

**Step 3: Write minimal implementation**

Create a small metrics type that owns:

- `cellHorizontalInset`
- `headerHeight`
- `rowHeight`
- `minimumColumnWidth`
- `maximumAutoWidth`
- `renderedWidth(forContentWidth:)`
- `clampedResizedWidth(start:translation:)`

Also move width calculation out of `ExcelGridView.initializeColumnWidths()` into a helper so the contract becomes testable.

**Step 4: Run test to verify it passes**

Run: `swift test --filter GridMetricsTests`
Expected: PASS

**Step 5: Commit**

```bash
git add Sources/xlsOne/Grid/GridMetrics.swift Sources/xlsOne/ContentView.swift Tests/xlsOneTests/GridMetricsTests.swift
git commit -m "refactor: extract grid column metrics"
```

### Task 2: Unify Header And Body Width Rendering

**Files:**
- Create: `Sources/xlsOne/Grid/GridColumnFrame.swift`
- Create: `Sources/xlsOne/Grid/GridHeaderCell.swift`
- Create: `Sources/xlsOne/Grid/GridBodyCell.swift`
- Modify: `Sources/xlsOne/ContentView.swift`
- Test: `Tests/xlsOneTests/ExcelGridLayoutTests.swift`

**Step 1: Write the failing test**

Add a macOS-hosted layout test that mounts a small `ExcelGridView` in an `NSHostingView`, collects header/body frames via a SwiftUI preference key, and asserts the same column uses the same width:

```swift
func testHeaderAndFirstBodyRowShareSameColumnWidth() {
    let frames = renderGridAndCaptureFrames()
    XCTAssertEqual(frames.header[0]?.width, frames.body[CellPosition(row: 0, col: 0)]?.width, accuracy: 0.5)
}
```

**Step 2: Run test to verify it fails**

Run: `swift test --filter ExcelGridLayoutTests/testHeaderAndFirstBodyRowShareSameColumnWidth`
Expected: FAIL before header/body rendering is normalized

**Step 3: Write minimal implementation**

- Replace the current ad hoc width usage in both `columnHeaders` and `ExcelCellView` with one shared frame wrapper.
- Stop rendering an interactive header inside the scroll content.
- Keep vertical spacing by adding a non-interactive top spacer with the shared header height instead of duplicating the header view tree.
- Ensure header cells and body cells both use the same outer width and border geometry.

**Step 4: Run test to verify it passes**

Run: `swift test --filter ExcelGridLayoutTests`
Expected: PASS

**Step 5: Commit**

```bash
git add Sources/xlsOne/Grid/GridColumnFrame.swift Sources/xlsOne/Grid/GridHeaderCell.swift Sources/xlsOne/Grid/GridBodyCell.swift Sources/xlsOne/ContentView.swift Tests/xlsOneTests/ExcelGridLayoutTests.swift
git commit -m "fix: align sticky header with grid body"
```

### Task 3: Make Resize Hover And Drag Reliable

**Files:**
- Create: `Sources/xlsOne/Grid/ColumnResizeHandle.swift`
- Create: `Sources/xlsOne/Grid/CursorManager.swift`
- Modify: `Sources/xlsOne/ContentView.swift`
- Test: `Tests/xlsOneTests/ColumnResizeControllerTests.swift`

**Step 1: Write the failing test**

Add tests around a small resize controller or helper:

```swift
func testBeginResizeCapturesStartingWidth() {
    let controller = ColumnResizeController()
    controller.beginResize(column: 2, width: 120)
    XCTAssertEqual(controller.draggingColumn, 2)
    XCTAssertEqual(controller.dragStartWidth, 120)
}

func testUpdateResizeAppliesTranslationAndClamp() {
    let controller = ColumnResizeController()
    controller.beginResize(column: 2, width: 120)
    XCTAssertEqual(controller.updatedWidth(translation: 30), 150)
}
```

**Step 2: Run test to verify it fails**

Run: `swift test --filter ColumnResizeControllerTests`
Expected: FAIL because the resize controller/helper does not exist yet

**Step 3: Write minimal implementation**

- Increase the effective hover area from `4pt` to at least `10pt`, while keeping the visible divider visually subtle.
- Give the handle an explicit height equal to the shared header height.
- Replace direct `NSCursor.set()` calls with a cursor helper that uses `push` on hover begin and `pop` on hover end, guarding against duplicate pushes.
- Keep drag behavior deterministic by routing start/update/end through a small helper instead of mutating several `@State` values inline.

**Step 4: Run test to verify it passes**

Run: `swift test --filter ColumnResizeControllerTests`
Expected: PASS

**Step 5: Commit**

```bash
git add Sources/xlsOne/Grid/ColumnResizeHandle.swift Sources/xlsOne/Grid/CursorManager.swift Sources/xlsOne/ContentView.swift Tests/xlsOneTests/ColumnResizeControllerTests.swift
git commit -m "fix: stabilize column resize cursor and drag state"
```

### Task 4: Regression Tests For Auto Width Initialization

**Files:**
- Modify: `Sources/xlsOne/ContentView.swift`
- Test: `Tests/xlsOneTests/GridAutoWidthTests.swift`

**Step 1: Write the failing test**

Add tests covering real dataset patterns:

```swift
func testAutoWidthUsesHeaderAndSampledRows() {
    let widths = ColumnWidthCalculator.defaultWidths(for: sampleRows)
    XCTAssertGreaterThan(widths[3]!, widths[0]!)
}

func testMissingCellsStillUseSharedWidthForSparseRows() {
    let widths = ColumnWidthCalculator.defaultWidths(for: sparseRows)
    XCTAssertNotNil(widths[2])
}
```

**Step 2: Run test to verify it fails**

Run: `swift test --filter GridAutoWidthTests`
Expected: FAIL before width calculation is fully extracted and covered

**Step 3: Write minimal implementation**

- Route `initializeColumnWidths()` through the extracted calculator.
- Preserve current min/max behavior unless testing proves it should change.
- Make sure sparse rows and empty trailing cells still yield deterministic column widths.

**Step 4: Run test to verify it passes**

Run: `swift test --filter GridAutoWidthTests`
Expected: PASS

**Step 5: Commit**

```bash
git add Sources/xlsOne/ContentView.swift Tests/xlsOneTests/GridAutoWidthTests.swift
git commit -m "test: cover grid auto width calculation"
```

## Automated Test Plan

The current suite is almost entirely business-logic coverage in `Tests/xlsOneCoreTests`. The only app-target test is a placeholder in `Tests/xlsOneTests/XlsOneTests.swift`. For this UI fix, add automated coverage in three layers:

1. Pure unit tests
   - `GridMetricsTests`
   - `ColumnResizeControllerTests`
   - `GridAutoWidthTests`
   - Purpose: make width math, clamping, and resize state deterministic and cheap to run.

2. macOS-hosted layout tests
   - `ExcelGridLayoutTests`
   - Mount `ExcelGridView` in `NSHostingView`
   - Capture header/body frames through preference keys or identifiable probes
   - Assert:
     - header column `N` width equals body column `N` width
     - header column `N` minX equals body column `N` minX within tolerance
     - after injecting a manual width override, both header/body reflect the same width

3. End-to-end smoke gate
   - Re-run full suite with `swift test`
   - Keep one manual smoke pass for actual pointer behavior until a dedicated UI-test host exists
   - Manual smoke checklist:
     - open a sheet with 5+ columns
     - verify sticky header borders align with row 1 borders
     - hover each divider and confirm resize cursor appears before drag
     - drag narrower than minimum and confirm width clamps
     - horizontal scroll after resizing and confirm sticky header remains aligned

## Risks And Mitigations

- Risk: SwiftPM app-target tests may make SwiftUI view inspection awkward.
  - Mitigation: extract math/state into standalone helpers first, then keep view-layout tests narrow and AppKit-hosted.

- Risk: cursor behavior may still be inconsistent if SwiftUI hover handling remains too thin.
  - Mitigation: fall back to a tiny `NSViewRepresentable` tracking area specifically for the resize handle.

- Risk: removing the duplicated scroll-content header could accidentally shift content vertically.
  - Mitigation: lock header height into shared metrics and cover it with the layout test plus a manual smoke pass.

## Definition Of Done

- Sticky column header and body columns align within sub-point tolerance in automated layout tests.
- Resize divider shows left-right cursor reliably on hover.
- Column drag updates both sticky header and body cells together.
- Full `swift test` passes.
- No regression in merge logic or existing sheet rendering.
