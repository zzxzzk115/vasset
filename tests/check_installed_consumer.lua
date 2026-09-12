function main(prefix)
    assert(prefix, "expected install prefix")
    local projectdir = os.projectdir()
    local consumer = path.join(projectdir, "tests/installed_consumer")
    local fixture = path.join(projectdir, "resources/splats/hornedlizard.spz")
    assert(os.isfile(fixture), "missing Gaussian fixture")
    os.execv(os.programfile(), {"f", "-y", "-P", consumer, "-m", "release", "--prefix=" .. path.absolute(prefix)})
    -- Installed archives are external link inputs; force relinking after a new installation.
    os.execv(os.programfile(), {"build", "-r", "-y", "-P", consumer})
    os.execv(os.programfile(), {"run", "-P", consumer, "installed-consumer", fixture})
end
