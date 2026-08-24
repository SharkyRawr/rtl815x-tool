class Rtl815xTool < Formula
  desc 'Realtek RTL8156 / RTL815x macOS Diagnostics & Control Toolkit'
  homepage 'https://github.com/SharkyRawr/rtl815x-tool'
  url 'https://github.com/SharkyRawr/rtl815x-tool/archive/refs/tags/v1.0.0.tar.gz'
  sha256 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
  head 'https://github.com/SharkyRawr/rtl815x-tool.git', branch: 'main'
  license 'CC-BY-NC-SA-4.0'

  depends_on :macos

  def install
    system 'make'
    bin.install 'rtl815x-tool'
  end

  test do
    assert_match 'Realtek RTL8156', shell_output("#{bin}/rtl815x-tool --help")
  end
end
