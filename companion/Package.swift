// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "OpenDisplayNXInput",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "OpenDisplayNXInput", targets: ["NXInput"])],
    targets: [
        .target(name: "CompanionCore"),
        .executableTarget(name: "NXInput", dependencies: ["CompanionCore"]),
        .testTarget(name: "CompanionCoreTests", dependencies: ["CompanionCore"])
    ]
)
