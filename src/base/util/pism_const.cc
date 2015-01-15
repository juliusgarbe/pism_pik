// Copyright (C) 2007--2014 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <petsc.h>
#include <petscfix.h>
#include <petsctime.h>

#include "PIO.hh"
#include "pism_const.hh"
#include <sstream>
#include <ctime>
#include <algorithm>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <cassert>

#include "NCVariable.hh"


//! \brief PISM verbosity level; determines how much gets printed to the
//! standard out.
static int verbosityLevel;

//! \brief Set the PISM verbosity level.
PetscErrorCode setVerbosityLevel(int level) {
  if ((level < 0) || (level > 5)) {
    SETERRQ(PETSC_COMM_SELF, 1,"verbosity level invalid");
  }
  verbosityLevel = level;
  return 0;  
}

//! \brief Get the verbosity level.
int getVerbosityLevel() {
  return verbosityLevel;
}

//! Print messages to standard out according to verbosity threshhold.
/*!
Verbosity level version of PetscPrintf.  We print according to whether 
(threshold <= verbosityLevel), in which case print, or (threshold > verbosityLevel)
in which case no print.

verbosityLevelFromOptions() actually reads the threshold.

The range 1 <= threshold <= 5  is enforced.

Use this method for messages and warnings which should
- go to stdout and
- appear only once (regardless of number of processors).

For each communicator, rank 0 does the printing. Calls from other
ranks are ignored.

Should not be used for reporting fatal errors.
 */
PetscErrorCode verbPrintf(const int threshold,
                          MPI_Comm comm, const char format[], ...) {
  PetscErrorCode ierr;
  int            rank;

  PetscFunctionBegin;

  extern FILE *petsc_history;
  assert(1 <= threshold && threshold <= 5);

  ierr = MPI_Comm_rank(comm, &rank); CHKERRQ(ierr);
  if (rank == 0) {
    va_list Argp;
    if (verbosityLevel >= threshold) {
      va_start(Argp, format);
      ierr = PetscVFPrintf(PETSC_STDOUT, format, Argp); CHKERRQ(ierr);
      va_end(Argp);
    }
    if (petsc_history) { // always print to history
      va_start(Argp, format);
      ierr = PetscVFPrintf(petsc_history, format, Argp); CHKERRQ(ierr);
      va_end(Argp);
    }
  }
  PetscFunctionReturn(0);
}


//! Prints rank (in process group PETSC_COMM_WORLD) to stderr.  Then attempts to end all processes.
/*!
Avoid using this if possible.  SETERRQ() should be used for all procedures that
return PetscErrorCode.  Generally needed for errors in constructors.

Printing the rank seems to be redundant because it will appear in brackets at start
of each call to PetscErrorPrintf().  But emphasis is useful, perhaps ...

Calls "MPI_Abort(PETSC_COMM_WORLD,3155)" to attempt to end all processes.
If this works main() will return value 3155.  The problem with PetscEnd() for
this purpose is that it is collective (presumably over PETSC_COMM_WORLD).
 */
void endPrintRank() {
  PetscMPIInt rank;
  if (!MPI_Comm_rank(PETSC_COMM_WORLD, &rank)) {
    PetscErrorPrintf("\n\n    rank %d process called endPrintRank()\n"
                         "    ending ...  \n\n",rank);
  } else {
    PetscErrorPrintf("\n\n    process with undeterminable rank called endPrintRank()\n"
                         "    ending ...  \n\n");
  }
  MPI_Abort(PETSC_COMM_WORLD,3155);
}


//! Returns true if `str` ends with `suffix` and false otherwise.
bool ends_with(std::string str, std::string suffix) {
  if (str.empty() == true && suffix.empty() == false)
    return false;

  if (str.rfind(suffix) + suffix.size() == str.size())
    return true;

  return false;
}


//! Checks if a vector of doubles is strictly increasing.
bool is_increasing(const std::vector<double> &a) {
  int len = (int)a.size();
  for (int k = 0; k < len-1; k++) {
    if (a[k] >= a[k+1])  return false;
  }
  return true;
}

//! Creates a time-stamp used for the history NetCDF attribute.
std::string pism_timestamp() {
  time_t now;
  tm tm_now;
  char date_str[50];
  now = time(NULL);
  localtime_r(&now, &tm_now);
  // Format specifiers for strftime():
  //   %F = ISO date format,  %T = Full 24 hour time,  %Z = Time Zone name
  strftime(date_str, sizeof(date_str), "%F %T %Z", &tm_now);

  return std::string(date_str);
}

//! Creates a string with the user name, hostname and the time-stamp (for history strings).
std::string pism_username_prefix(MPI_Comm com) {
  PetscErrorCode ierr;

  char username[50];
  ierr = PetscGetUserName(username, sizeof(username));
  if (ierr != 0)
    username[0] = '\0';
  char hostname[100];
  ierr = PetscGetHostName(hostname, sizeof(hostname));
  if (ierr != 0)
    hostname[0] = '\0';
  
  std::ostringstream message;
  message << username << "@" << hostname << " " << pism_timestamp() << ": ";

  std::string result = message.str();
  int length = result.size();
  MPI_Bcast(&length, 1, MPI_INT, 0, com);

  result.resize(length);
  MPI_Bcast(&result[0], length, MPI_CHAR, 0, com);

  return result;
}

//! \brief Uses argc and argv to create the string with current PISM
//! command-line arguments.
std::string pism_args_string() {
  int argc;
  char **argv;
  PetscGetArgs(&argc, &argv);

  std::string cmdstr, argument;
  for (int j = 0; j < argc; j++) {
    argument = argv[j];

    // enclose arguments containing spaces with double quotes:
    if (argument.find(" ") != std::string::npos) argument = "\"" + argument + "\"";

    cmdstr += std::string(" ") + argument;
  }
  cmdstr += "\n";

  return cmdstr;
}

//! \brief Adds a suffix to a filename.
/*!
 * Returns filename + separator + suffix + .nc if the original filename had the
 * .nc suffix, otherwise filename + separator. If the old filename had the form
 * "name + separator + more stuff + .nc", then removes the string after the
 * separator.
 */
std::string pism_filename_add_suffix(std::string filename, std::string separator, std::string suffix) {
  std::string basename = filename, result;

  // find where the separator begins:
  std::string::size_type j = basename.rfind(separator);
  if (j == std::string::npos) {
    j = basename.rfind(".nc");
  }

  // if the separator was not found, find the .nc suffix:
  if (j == std::string::npos) {
    j = basename.size();
  }

  // cut off everything starting from the separator (or the .nc suffix):
  basename.resize(static_cast<int>(j));

  result = basename + separator + suffix;

  if (ends_with(filename, ".nc"))
    result += ".nc";

  return result;
}

//! \brief Finalizes PETSc and MPI. Replaces PetscEnd().
/*!
 * This is necessary if PETSc is using a subset of all the processors in
 * MPI_COMM_WORLD. Using PetscEnd() in this case leaves processes \b not
 * running PETSc hanging waiting for a MPI_Finalize() call. (PetscFinalize()
 * only calls MPI_Finalize() if PetscInitialize() called MPI_Init().)
 */
void PISMEnd() {
  int flag;
  PetscFinalize();

  MPI_Finalized(&flag);
  if ( ! flag )
    MPI_Finalize();

  exit(0);
}

void PISMEndQuiet() {
  PetscOptionsSetValue("-options_left","no");
  PISMEnd();
}

PetscErrorCode PISMGetTime(PetscLogDouble *result) {
#if PETSC_VERSION_LT(3,4,0)
  PetscErrorCode ierr = PetscGetTime(result); CHKERRQ(ierr);
#else
  PetscErrorCode ierr = PetscTime(result); CHKERRQ(ierr);
#endif
  return 0;
}
