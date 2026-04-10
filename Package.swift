// swift-tools-version:5.7
import PackageDescription

let package = Package(
    name: "xlsOne",
    platforms: [.macOS(.v12)],
    products: [
        .library(name: "xlsOneCore", targets: ["xlsOneCore"]),
        .executable(name: "xlsOne", targets: ["xlsOne"]),
        .executable(name: "xlsOneCLI", targets: ["xlsOneCLI"])
    ],
    dependencies: [
        .package(url: "https://github.com/CoreOffice/CoreXLSX.git", from: "0.14.1")
    ],
    targets: [
        .target(
            name: "xlsOneCore",
            dependencies: ["CoreXLSX"]
        ),
        .executableTarget(
            name: "xlsOne",
            dependencies: ["xlsOneCore"]
        ),
        .executableTarget(
            name: "xlsOneCLI",
            dependencies: ["xlsOneCore"]
        ),
        .testTarget(
            name: "xlsOneCoreTests",
            dependencies: ["xlsOneCore"]
        ),
        .testTarget(
            name: "xlsOneTests",
            dependencies: ["xlsOne"]
        )
    ]
)
