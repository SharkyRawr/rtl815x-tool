class Rtl815xTool < Formula
  desc 'Realtek RTL8156 / RTL815x macOS Diagnostics & Control Toolkit'
  homepage 'https://github.com/SharkyRawr/rtl815x-tool'
  url 'https://github.com/SharkyRawr/rtl815x-tool/archive/refs/tags/v1.0.0.tar.gz'
  sha256 '94c78ffe651bd0cb58dde6aabf0b9cebc059118ed883ddb138e8dd298a289c28'
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
