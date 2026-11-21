// swift-tools-version:5.9
// SPDX-License-Identifier: MIT
// ClayWind for Swift: a Swift layer over the C API (include/claywind.h), and the counter example in Swift.
// Build claywind-capi first (ceesafec build in claywind-capi, which writes out/claywind_capi.a), then `swift build`.
import Foundation
import PackageDescription

/// claywind-capi's archive (ceesafec build there): named claywind_capi.a, so it is linked by its path, not with -l.
let archive = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    .appendingPathComponent("../../out/claywind_capi.a").standardized.path

let package = Package(
    name: "ClayWind",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "ClayWind", targets: ["ClayWind"]),
        .executable(name: "counter", targets: ["Counter"]),
    ],
    targets: [
        // The C API and CeeSafe's runtime header, and the archive that holds both (claywind-capi/out).
        .target(
            name: "CClayWind",
            linkerSettings: [
                .unsafeFlags([archive]),
                .linkedFramework("AppKit"),
                .linkedFramework("QuartzCore"),
                .linkedFramework("CoreText"),
                .linkedFramework("CoreGraphics"),
            ]
        ),
        .target(name: "ClayWind", dependencies: ["CClayWind"]),
        .executableTarget(name: "Counter", dependencies: ["ClayWind"]),
    ]
)
