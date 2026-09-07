class Rtl815xTool < Formula
  desc 'Realtek RTL8156 / RTL815x macOS Diagnostics & Control Toolkit'
  homepage 'https://github.com/SharkyRawr/rtl815x-tool'
  url 'https://github.com/SharkyRawr/rtl815x-tool/archive/refs/tags/v1.0.1.tar.gz'
  sha256 '747fd876160020e7ec4c2c8b1b486387248b38c7ee4435d9aebd0647fd754d33'
  head 'https://github.com/SharkyRawr/rtl815x-tool.git', branch: 'main'
  license 'CC-BY-NC-SA-4.0'

  depends_on :macos

  def install
    system 'make'
    bin.install 'rtl815x-tool'
  end

  test do
    assert_match 'Realtek RTL8156', shell_output("#{bin}/rtl815x-tool --help")
    assert_match 'v1.0.1', shell_output("#{bin}/rtl815x-tool --help")
  end
end
