class Dockctl < Formula
  desc "Dell WD22TB4 telemetry and experimental fan control for macOS"
  homepage "https://github.com/Frulko/macos-dell-WD22T04-controller"
  url "https://github.com/Frulko/macos-dell-WD22T04-controller/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "61c10d713e23c080487b1593ad8c2affbb06b2fc49801b429bda0ff1de022643"
  version "0.1.0"
  license all_of: ["MIT", "Apache-2.0"]
  head "https://github.com/Frulko/macos-dell-WD22T04-controller.git", branch: "main"

  depends_on macos: :ventura

  def install
    system "make", "all"
    bin.install "dockctl"
    lib.install "libdock.a", "libdock.dylib"
    (include/"dockctl").install "include/dock.h", "include/module.modulemap"
    pkgshare.install "examples/info.c", "examples/watch.c", "NOTICE"
  end

  def caveats
    <<~EOS
      Thermal commands require sudo and AppleHPMLib v3 (tested on Apple Silicon).
      Intel binaries are build-tested; Intel thermal control is not established.
      Start with: dockctl info
      Read experimental silence limitations before running: dockctl watch --silence
    EOS
  end

  test do
    assert_match "Usage", shell_output("#{bin}/dockctl --help")
    assert_match '"silence_watch"', shell_output("#{bin}/dockctl features --json")
  end
end
