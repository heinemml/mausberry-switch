# Building & packaging from source

This guide covers building a Debian package for Debian 7 "Wheezy", and Debian 8 "Jessie" only. Other distributions are not officially supported.

# Before you begin

 - Briefly skim the Debian [BuildingTutorial][1] Wiki article.

# Build instructions

On your Raspberry Pi (or other Debian machine of choice),

```bash
# Install things needed for compiling and packaging
$ sudo apt-get install build-essential fakeroot devscripts git-buildpackage

# Grab a copy of the source code
$ git clone https://github.com/t-richards/mausberry-switch

# Check out the correct branch
$ cd mausberry-switch
$ git checkout 0.6

# Build the package
$ git-buildpackage
```

 [1]: https://wiki.debian.org/BuildingTutorial
