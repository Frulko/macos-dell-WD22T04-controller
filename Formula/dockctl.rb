class Dockctl < Formula
  desc "Dell WD22TB4 telemetry and experimental fan control for macOS"
  homepage "https://github.com/Frulko/macos-dell-WD22T04-controller"
  # Stable URL and SHA256 are inserted by the release workflow.
  head "https://github.com/Frulko/macos-dell-WD22T04-controller.git", branch: "main"
  license all_of: ["MIT", "Apache-2.0"]

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
