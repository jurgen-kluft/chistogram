package chistogram

import (
	cbase "github.com/jurgen-kluft/cbase/package"
	"github.com/jurgen-kluft/ccode/denv"
	cfile "github.com/jurgen-kluft/cfile/package"
	cunittest "github.com/jurgen-kluft/cunittest/package"
)

// GetPackage returns the package object of 'chistogram'
func GetPackage() *denv.Package {
	// Dependencies
	cunittestpkg := cunittest.GetPackage()
	cfilepkg := cfile.GetPackage()
	cbasepkg := cbase.GetPackage()

	// The main (chistogram) package
	mainpkg := denv.NewPackage("chistogram")
	mainpkg.AddPackage(cunittestpkg)
	mainpkg.AddPackage(cfilepkg)
	mainpkg.AddPackage(cbasepkg)

	// 'chistogram' library
	mainlib := denv.SetupDefaultCppLibProject("chistogram", "github.com\\jurgen-kluft\\chistogram")
	mainlib.Dependencies = append(mainlib.Dependencies, cfilepkg.GetMainLib())
	mainlib.Dependencies = append(mainlib.Dependencies, cbasepkg.GetMainLib())

	// 'chistogram' unittest project
	maintest := denv.SetupDefaultCppTestProject("chistogram_test", "github.com\\jurgen-kluft\\chistogram")
	maintest.Dependencies = append(maintest.Dependencies, cunittestpkg.GetMainLib())
	maintest.Dependencies = append(maintest.Dependencies, cfilepkg.GetMainLib())
	maintest.Dependencies = append(maintest.Dependencies, cbasepkg.GetMainLib())
	maintest.Dependencies = append(maintest.Dependencies, mainlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddUnittest(maintest)
	return mainpkg
}
