class Rtl815xTool < Formula
  desc 'Realtek RTL8156 / RTL815x macOS Diagnostics & Control Toolkit'
  homepage 'https://github.com/SharkyRawr/rtl815x-tool'
  url 'https://github.com/SharkyRawr/rtl815x-tool/archive/refs/tags/v1.0.2.tar.gz'
  sha256 '12c41baee9086ba8fe0e52276a0a9a76267f92a62c5d537819bce08f8c9114a4'
  head 'https://github.com/SharkyRawr/rtl815x-tool.git', branch: 'main'
  license 'CC-BY-NC-SA-4.0'

  depends_on :macos

  def install
    system 'make'
    bin.install 'rtl815x-tool'
  end

  test do
    assert_match 'Realtek RTL8156', shell_output("#{bin}/rtl815x-tool --help")
    assert_match 'v1.0.2', shell_output("#{bin}/rtl815x-tool --help")
  end
end
