from conans import ConanFile, CMake

class DriveFSConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch", "cppstd"
    requires = ("boost/1.69.0@conan/stable",  "mongo-cxx-driver/3.2.0@bincrafters/stable",
    "mongo-c-driver/1.9.4@bincrafters/stable", "cpprestsdk/2.10.10@bincrafters/stable",
    "abseil/20180600@bincrafters/stable","OpenSSL/1.0.2r@conan/stable",
    "jemalloc/5.0.1@ess-dmsc/stable"
    )

    generators = "cmake"
    default_options = {"OpenSSL:shared": False, "boost:shared": False,
        "mongo-cxx-driver:shared": False, "mongo-c-driver:shared": False, "cpprestsdk:shared": False,
        "jemalloc:shared": False
    }
    build_policy = "missing"


    def imports(self):
        self.copy("*.dll", dst="bin", src="bin") # From bin to bin
        self.copy("*.dylib*", dst="bin", src="lib") # From lib to bin
        self.copy("*.so*", dst="lib", src="lib") # From lib to bin
        self.copy("*.a*", dst="lib", src="lib") # From lib to bin
        self.copy("*.h*", dst="include", src="include") # From lib to bin

    def build(self):
        if self.settings.os != "Windows":
            self.settings.compiler.libcxx = "libstdc++11"
        self.settings.cppstd=11
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

