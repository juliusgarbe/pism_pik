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

#ifndef __pism_const_hh
#define __pism_const_hh

#include <petsc.h>
#include <string>
#include <vector>
#include <set>

extern const char *PISM_Revision;
extern const char *PISM_DefaultConfigFile;

enum PismMask {
  MASK_UNKNOWN          = -1,
  MASK_ICE_FREE_BEDROCK = 0,
  MASK_GROUNDED         = 2,
  MASK_FLOATING         = 3,
  MASK_ICE_FREE_OCEAN   = 4
};

const int TEMPORARY_STRING_LENGTH = 32768; // 32KiB ought to be enough.

bool is_increasing(const std::vector<double> &a);

PetscErrorCode setVerbosityLevel(int level);
int       getVerbosityLevel();
PetscErrorCode verbPrintf(const int thresh, MPI_Comm comm,const char format[],...);

void endPrintRank();

#ifndef __GNUC__
#  define  __attribute__(x)  /* nothing */
#endif
void PISMEnd()  __attribute__((noreturn));
void PISMEndQuiet()  __attribute__((noreturn));

std::string pism_timestamp();
std::string pism_username_prefix(MPI_Comm com);
std::string pism_args_string();
std::string pism_filename_add_suffix(std::string filename, std::string separator, std::string suffix);

PetscErrorCode PISMGetTime(PetscLogDouble *result);

bool ends_with(std::string str, std::string suffix);

inline bool set_contains(std::set<std::string> S, std::string name) {
  return (S.find(name) != S.end());
}

inline PetscErrorCode PISMGlobalMin(double *local, double *result, MPI_Comm comm)
{
  return MPI_Allreduce(local,result,1,MPIU_REAL,MPI_MIN,comm);
}

inline PetscErrorCode PISMGlobalMax(double *local, double *result, MPI_Comm comm)
{
  return MPI_Allreduce(local,result,1,MPIU_REAL,MPI_MAX,comm);
}

inline PetscErrorCode PISMGlobalSum(double *local, double *result, MPI_Comm comm)
{
  return MPI_Allreduce(local,result,1,MPIU_REAL,MPI_SUM,comm);
}

#endif
