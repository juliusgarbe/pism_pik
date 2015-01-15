// Copyright (C) 2004-2014 Jed Brown, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

static char help[] =
"Ice sheet driver for PISM (SIA and SSA) verification.  Uses exact solutions\n"
"  to various coupled subsystems.  Computes difference between exact solution\n"
"  and numerical solution.  Can also just compute exact solution (-eo).\n"
"  Currently implements tests A, B, C, D, E, F, G, H, K, L.\n\n";

#include <cctype>               // toupper
#include <string>
#include <algorithm>            // std::transform()
#include <petscdmda.h>
#include "IceGrid.hh"
#include "verif/iceCompModel.hh"

#include "POConstant.hh"
#include "pism_options.hh"

// a wrapper that seems to be necessary to make std::transform below work
static inline char pism_toupper(char c)
{
    return (char)std::toupper(c);
}

int main(int argc, char *argv[]) {
  PetscErrorCode  ierr;
  MPI_Comm        com;

  PetscInitialize(&argc, &argv, PETSC_NULL, help);

  com = PETSC_COMM_WORLD;
      
  /* This explicit scoping forces destructors to be called before PetscFinalize() */
  {
    ierr = verbosityLevelFromOptions(); CHKERRQ(ierr);

    ierr = verbPrintf(2, com, "PISMV %s (verification mode)\n",
                      PISM_Revision); CHKERRQ(ierr);
    ierr = stop_on_version_option(); CHKERRQ(ierr);

    std::vector<std::string> required;
    required.push_back("-test");
    ierr = show_usage_check_req_opts(com, "pismv", required,
        "  pismv -test x [-no_report] [-eo] [OTHER PISM & PETSc OPTIONS]\n"
        "where:\n"
        "  -test x     SIA-type verification test (x = A|B|C|D|E|F|G|H|K|L)\n"
        "  -no_report  do not give error report at end of run\n"
        "  -eo         do not do numerical run; exact solution only\n"
        "(see User's Manual for tests I and J).\n"
        ); CHKERRQ(ierr);

    PISMUnitSystem unit_system(NULL);
    PISMConfig config(com, "pism_config", unit_system),
      overrides(com, "pism_overrides", unit_system);
    ierr = init_config(com, config, overrides, true); CHKERRQ(ierr);

    config.set_flag("use_eta_transformation", false);
    config.set_string("calendar", "none");

    IceGrid g(com, config);

    // determine test (and whether to report error)
    std::string testname = "A";
    bool   test_chosen;
    ierr = PetscOptionsBegin(g.com, "", "Options specific to PISMV", ""); CHKERRQ(ierr);
    {
      ierr = PISMOptionsString("-test", "Specifies PISM verification test",
                               testname, test_chosen); CHKERRQ(ierr);
    }
    ierr = PetscOptionsEnd(); CHKERRQ(ierr);

    // transform to uppercase:
    transform(testname.begin(), testname.end(), testname.begin(), pism_toupper);

    // actually construct and run one of the derived classes of IceModel
    // run derived class for compensatory source SIA solutions
    // (i.e. compensatory accumulation or compensatory heating)
    IceCompModel m(g, config, overrides, testname[0]);
    ierr = m.setExecName("pismv"); CHKERRQ(ierr);

    ierr = m.init(); CHKERRQ(ierr);

    ierr = m.run(); CHKERRQ(ierr);
    ierr = verbPrintf(2,com, "done with run\n"); CHKERRQ(ierr);

    ierr = m.reportErrors();  CHKERRQ(ierr);

    // provide a default output file name if no -o option is given.
    ierr = m.writeFiles("unnamed.nc"); CHKERRQ(ierr);
  }

  ierr = PetscFinalize(); CHKERRQ(ierr);

  return 0;
}

