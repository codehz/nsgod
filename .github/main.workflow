workflow "New workflow" {
  on = "release"
  resolves = ["JasonEtco/upload-to-release@master", "JasonEtco/upload-to-release@master-1"]
}

action "codehz/arch-cmake-builder@master" {
  uses = "codehz/arch-cmake-builder@master"
  args = "CXXFLAGS=-static LDFLAGS=-static"
}

action "JasonEtco/upload-to-release@master" {
  uses = "JasonEtco/upload-to-release@master"
  needs = ["codehz/arch-cmake-builder@master"]
  args = "build/nsgod application/x-executable"
  secrets = ["GITHUB_TOKEN"]
}

action "JasonEtco/upload-to-release@master-1" {
  uses = "JasonEtco/upload-to-release@master"
  needs = ["codehz/arch-cmake-builder@master"]
  args = "build/nsctl application/x-executable"
  secrets = ["GITHUB_TOKEN"]
}
