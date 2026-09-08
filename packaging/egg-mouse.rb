# Homebrew formula for the tap daohhuynh/homebrew-egg-mouse.
#
# THIS FILE IS THE SOURCE OF TRUTH; the copy in the tap repository is generated
# from it by Tools/update-tap.sh. Editing the tap directly is how the two drift.
#
# WHY A FORMULA AND NOT A CASK. A cask downloads a prebuilt .app, and anything
# a downloader fetches gets macOS's com.apple.quarantine attribute, which is
# what triggers the "Apple cannot verify this app" wall. A formula builds from
# source on the user's own machine, and nothing Homebrew compiles is ever
# quarantined. So this route has no Gatekeeper prompt at all, which the .dmg on
# the Releases page cannot avoid without a paid Developer ID certificate.
class EggMouse < Formula
  desc "Config tool and firmware flasher for the Endgame Gear OP1 8k v2"
  homepage "https://github.com/daohhuynh/egg-mouse"
  url "https://github.com/daohhuynh/egg-mouse/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "REPLACED_BY_UPDATE_TAP"
  license "Apache-2.0"

  depends_on "cmake" => :build
  depends_on macos: :sonoma         # the app's LSMinimumSystemVersion is 14.0

  # No `depends_on xcode: :build`, deliberately. It makes Homebrew police the
  # Xcode VERSION, which failed here on a machine with Xcode 16.4 because
  # Homebrew wanted 26.3, and it would also exclude anyone who has only the
  # Command Line Tools. The CLT ship swiftc and the macOS SDK, which is all the
  # SwiftUI front end needs. build-app.sh checks for swiftc itself and says so
  # plainly if it is missing.

  # No hidapi dependency on purpose. The macOS HID backend is compiled from the
  # pinned upstream sources in third_party/hidapi/, so the binaries depend on
  # nothing outside /System and /usr/lib. See that directory's README.

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args,
                    "-DEGG_VENDORED_HIDAPI=ON"
    system "cmake", "--build", "build", "--target", "egg-config", "egg-flash"

    # ORDER MATTERS AND THIS IS NOT STYLISTIC. `bin.install` MOVES the files it
    # is given rather than copying them, so doing it before this line leaves
    # build/egg-config gone and build-app.sh fails with "not found". That is
    # exactly how the first attempt at this formula broke.
    #
    # The GUI shells out to the two CLIs and owns no write path; see
    # engineering-rules.md 3. Bundling them inside the .app keeps it working
    # even if the Homebrew bin directory is not on a GUI process's PATH, which
    # for an app launched from Finder it usually is not.
    system "./Tools/build-app.sh", "--with-tools", "build",
                                   "--version", version.to_s,
                                   "--out", "EGG Mouse.app"
    prefix.install "EGG Mouse.app"

    bin.install "build/egg-config", "build/egg-flash"
  end

  def caveats
    <<~EOS
      The command line tools are ready to use:

        egg-config show          what your mouse is currently set to
        egg-flash  help          the flasher, and what it refuses to do

      The app was installed to:

        #{opt_prefix}/EGG Mouse.app

      To put it in Launchpad and Spotlight:

        ln -sfn "#{opt_prefix}/EGG Mouse.app" "/Applications/EGG Mouse.app"

      egg-flash REWRITES FIRMWARE and a failed flash may leave a mouse that
      does not work. It is unofficial, is not endorsed by Endgame Gear, and
      comes with no warranty. Read the Firmware section of the README first.

      If your mouse ever stops responding: hold LEFT and RIGHT together, plug
      the cable in while still holding, keep holding a few seconds, release.
      It comes back as a re-flashable bootloader.
    EOS
  end

  test do
    # Deliberately not a command that opens the device: `brew test` has to pass
    # on a machine with no mouse attached, and nothing in a test should ever
    # reach the hardware.
    #
    # The exit codes are not 0 and that is by design, not a bug being papered
    # over: both CLIs route `help` through the same usage() that handles a
    # missing verb, so it exits non-zero. Tests/test_flash_restore.sh says so
    # in as many words. Pinning them here means this formula notices if that
    # ever changes silently.
    assert_match "egg-flash", shell_output("#{bin}/egg-flash help", 2)
    assert_match "egg-config", shell_output("#{bin}/egg-config help 2>&1", 1)
    assert_predicate prefix/"EGG Mouse.app/Contents/MacOS/EGG Mouse", :exist?
  end
end
