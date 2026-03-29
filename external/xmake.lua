target("dds-ktx")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
    set_default(false)
	add_headerfiles("dds-ktx/dds-ktx.h")
    add_includedirs("dds-ktx", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("miniply")
    set_kind("static")
    set_default(false)
    add_headerfiles("miniply/**.h")
    add_includedirs("miniply", {public = true}) -- public: let other targets to auto include
    add_files("miniply/**.cpp")

target("spz")
    set_kind("static")
    set_default(false)
    add_headerfiles("spz/**.h")
    add_includedirs("spz/src/cc", {public = true}) -- public: let other targets to auto include
    add_files("spz/**.cc")
    add_packages("zlib")

includes("vfilesystem")
