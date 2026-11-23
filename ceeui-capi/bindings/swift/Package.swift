// swift-tools-version:5.9
// SPDX-License-Identifier: MIT
// CeeUI for Swift: a Swift layer over the C API (include/ceeui.h), and the counter example in Swift.
// Build ceeui-capi first (ceesafec build in ceeui-capi, which writes out/ceeui_capi.a), then `swift build`.
import Foundation
import PackageDescription

/// ceeui-capi's archive (ceesafec build there): named ceeui_capi.a, so it is linked by its path, not with -l.
let archive = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    .appendingPathComponent("../../out/ceeui_capi.a").standardized.path

let package = Package(
    name: "CeeUI",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "CeeUI", targets: ["CeeUI"]),
        .executable(name: "counter", targets: ["Counter"]),
    ],
    targets: [
        // The C API and CeeSafe's runtime header, and the archive that holds both (ceeui-capi/out).
        .target(
            name: "CCeeUI",
            linkerSettings: [
                .unsafeFlags([archive]),
                .linkedFramework("AppKit"),
                .linkedFramework("QuartzCore"),
                .linkedFramework("CoreText"),
                .linkedFramework("CoreGraphics"),
            ]
        ),
        .target(name: "CeeUI", dependencies: ["CCeeUI"]),
        .executableTarget(name: "Counter", dependencies: ["CeeUI"]),
    ]
)
