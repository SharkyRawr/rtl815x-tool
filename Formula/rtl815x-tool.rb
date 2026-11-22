class Rtl815xTool < Formula
  desc 'Realtek RTL8156 / RTL815x macOS Diagnostics & Control Toolkit'
  homepage 'https://github.com/SharkyRawr/rtl815x-tool'
  url 'https://github.com/SharkyRawr/rtl815x-tool/archive/refs/tags/v1.0.4.tar.gz'
  sha256 'fe4ea7edcf909aba4aa4b289fbe5baeabf1572b900d73bb7c47fb9c2a2926798'
  head 'https://github.com/SharkyRawr/rtl815x-tool.git', branch: 'main'
  license 'CC-BY-NC-SA-4.0'

  depends_on :macos

  def install
    system 'make'
    bin.install 'rtl815x-tool'
  end

  test do
    assert_match 'Realtek RTL8156', shell_output("#{bin}/rtl815x-tool --help")
    assert_match 'v1.0.4', shell_output("#{bin}/rtl815x-tool --help")
  end
end
