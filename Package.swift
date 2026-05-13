// swift-tools-version:5.7
import PackageDescription

let package = Package(
    name: "xlsOne",
    platforms: [.macOS(.v12)],
    products: [
        .library(name: "xlsOneCore", targets: ["xlsOneCore"]),
        .library(name: "xlsOneUI", targets: ["xlsOneUI"]),
        .library(name: "xlsOneLicense", targets: ["xlsOneLicense"]),
        .executable(name: "xlsOne", targets: ["xlsOne"]),
        .executable(name: "xlsOneSnapshot", targets: ["xlsOneSnapshot"])
    ],
    dependencies: [
        .package(url: "https://github.com/CoreOffice/CoreXLSX.git", from: "0.14.1")
    ],
    targets: [
        .target(
            name: "xlsOneCore",
            dependencies: ["CoreXLSX"],
            resources: [.process("Resources")]
        ),
        .target(
            name: "xlsOneLicense",
            dependencies: []
        ),
        .target(
            name: "xlsOneUI",
            dependencies: ["xlsOneCore", "xlsOneLicense"]
        ),
        .executableTarget(
            name: "xlsOne",
            dependencies: ["xlsOneUI"]
        ),
        .executableTarget(
            name: "xlsOneSnapshot",
            dependencies: ["xlsOneCore"]
        ),
        .testTarget(
            name: "xlsOneCoreTests",
            dependencies: ["xlsOneCore"]
        ),
        .testTarget(
            name: "xlsOneTests",
            dependencies: ["xlsOneUI"]
        )
    ]
)
