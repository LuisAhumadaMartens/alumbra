// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "AlumbraConsumer",
    platforms: [
        .iOS(.v17),
        .tvOS(.v17)
    ],
    products: [
        .library(name: "AlumbraConsumer", targets: ["AlumbraConsumer"])
    ],
    dependencies: [],
    targets: [
        .binaryTarget(name: "Alumbra", path: "Frameworks/Alumbra.xcframework"),
        .target(name: "AlumbraConsumer", dependencies: ["Alumbra"]),
        .testTarget(name: "AlumbraConsumerTests", dependencies: ["AlumbraConsumer", "Alumbra"])
    ]
)
