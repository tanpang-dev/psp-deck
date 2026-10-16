// swift-tools-version:5.10
import PackageDescription

let package = Package(
    name: "PSPDeck",
    platforms: [.macOS("26.0")],  // Homebrew libusb is built for 26+
    targets: [
        .systemLibrary(name: "CLibUSB", pkgConfig: "libusb-1.0", providers: [.brew(["libusb"])]),
        .target(name: "DeckProto"),
        .executableTarget(name: "PSPDeck", dependencies: ["CLibUSB", "DeckProto"]),
    ]
)
