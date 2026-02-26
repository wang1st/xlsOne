// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "xlsOne",
    platforms: [
        .macOS(.v12)
    ],
    products: [
        .library(
            name: "xlsOneCore",
            targets: ["xlsOneCore"]
        )
    ],
    dependencies: [
        .package(url: "https://github.com/CoreOffice/CoreXLSX", from: "0.14.0"),
        .package(url: "https://github.com/apple/swift-argument-parser", from: "1.2.0")
    ],
    targets: [
        .target(
            name: "xlsOneCore",
            dependencies: [
                "CoreXLSX"
            ]
        ),
        // CLI temporarily disabled
        // .executableTarget(
        //     name: "xlsOneCLI",
        //     dependencies: [
        //         "xlsOneCore",
        //         .product(name: "ArgumentParser", package: "swift-argument-parser")
        //     ]
        // ),
        .executableTarget(
            name: "xlsOne",
            dependencies: [
                "xlsOneCore",
                "CoreXLSX"
            ]
        ),
        .testTarget(
            name: "xlsOneTests",
            dependencies: ["xlsOneCore"],
            path: "Tests"
        )
    ]
)
