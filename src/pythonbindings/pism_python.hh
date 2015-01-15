// Copyright (C) 2011, 2014 David Maxwell
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


#ifndef _PISM_PYTHON_
#define _PISM_PYTHON_
#include "petsc.h"

PetscErrorCode globalMax(double local_max, double *result, MPI_Comm comm);
PetscErrorCode globalMin(double local_min, double *result, MPI_Comm comm);
PetscErrorCode globalSum(double local_sum, double *result, MPI_Comm comm);

PetscErrorCode optionsGroupBegin(MPI_Comm comm,const char *prefix,const char *mess,const char *sec);
void optionsGroupNext();
bool optionsGroupContinue();
PetscErrorCode optionsGroupEnd();

void set_abort_on_sigint(bool abort);

#endif
