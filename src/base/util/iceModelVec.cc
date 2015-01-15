// Copyright (C) 2008--2014 Ed Bueler, Constantine Khroulev, and David Maxwell
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

#include <gsl/gsl_math.h>       // gsl_isnan()

#include "pism_const.hh"
#include "iceModelVec.hh"
#include "PIO.hh"
#include "PISMTime.hh"
#include "IceGrid.hh"
#include "LocalInterpCtx.hh"
#include "PISMConfig.hh"
#include <assert.h>

IceModelVec::IceModelVec() {
  access_counter = 0;
  array = NULL;

  m_da = NULL;
  m_da_stencil_width = 1;
  m_dof = 1;                    // default
  begin_end_access_use_dof = true;

  grid = NULL;

  m_has_ghosts = true;

  m_n_levels = 1;
  m_name = "unintialized variable";

  // would resize "vars", but "grid" is not initialized, and so we
  // cannot get the unit system:
  // vars.resize(dof);
  reset_attrs(0);

  state_counter = 0;

  m_v = NULL;

  zlevels.resize(1);
  zlevels[0] = 0.0;
}

//! \brief Get the object state counter.
/*!
 * This method returns the "revision number" of an IceModelVec.
 *
 * It can be used to determine it a field was updated and if a certain
 * computation needs to be re-done. One example is computing the smoothed bed
 * for the SIA computation, which is only necessary if the bed deformation code
 * fired.
 *
 * See also inc_state_counter().
 */
int IceModelVec::get_state_counter() const {
  return state_counter;
}

//! \brief Increment the object state counter.
/*!
 * See the documentation of get_state_counter(). This method is the \b only way
 * to increment the state counter. It is \b not modified or automatically
 * updated.
 */
void IceModelVec::inc_state_counter() {
  state_counter++;
}

unsigned int IceModelVec::get_stencil_width() const {
  if (m_has_ghosts) {
    return m_da_stencil_width;
  } else {
    return 0;
  }
}

IceModelVec::~IceModelVec() {
  destroy();
}

//! Returns true if create() was called and false otherwise.
bool IceModelVec::was_created() {
  return (m_v != NULL);
}

//! Returns the grid type of an IceModelVec. (This is the way to figure out if an IceModelVec is 2D or 3D).
unsigned int IceModelVec::get_ndims() {
  if (zlevels.size() > 1) return 3;

  return 2;
}

//! Set the time independent flag for all variables corresponding to this IceModelVec instance.
/** A "time independent" IceModelVec will be saved to a NetCDF
    variable which does not depend on the "time" dimension.
 */
void IceModelVec::set_time_independent(bool flag) {
  for (unsigned int j = 0; j < m_dof; ++j) {
    m_metadata[j].set_time_independent(flag);
  }
}

//! \brief De-allocates an IceModelVec object.
PetscErrorCode  IceModelVec::destroy() {
  PetscErrorCode ierr;

  if (m_v != NULL) {
    ierr = VecDestroy(&m_v); CHKERRQ(ierr);
    m_v = NULL;
  }

  // map-plane viewers:
  {
    std::map<std::string,PetscViewer>::iterator i;
    for (i = map_viewers.begin(); i != map_viewers.end(); ++i) {
      if (i->second != NULL) {
        ierr = PetscViewerDestroy(&i->second); CHKERRQ(ierr);
      }
    }
  }

  assert(access_counter == 0);

  return 0;
}

//! Result: min <- min(v[j]), max <- max(v[j]).
/*!
PETSc manual correctly says "VecMin and VecMax are collective on Vec" but
GlobalMax,GlobalMin \e are needed, when m_has_ghosts==true, to get correct
values because Vecs created with DACreateLocalVector() are of type
VECSEQ and not VECMPI.  See src/trypetsc/localVecMax.c.
 */
PetscErrorCode IceModelVec::range(double &min, double &max) {
  double my_min = 0.0, my_max = 0.0, gmin = 0.0, gmax = 0.0;
  PetscErrorCode ierr;
  assert(m_v != NULL);

  ierr = VecMin(m_v, NULL, &my_min); CHKERRQ(ierr);
  ierr = VecMax(m_v, NULL, &my_max); CHKERRQ(ierr);

  if (m_has_ghosts) {
    // needs a reduce operation; use PISMGlobalMax;
    ierr = PISMGlobalMin(&my_min, &gmin, grid->com); CHKERRQ(ierr);
    ierr = PISMGlobalMax(&my_max, &gmax, grid->com); CHKERRQ(ierr);
    min = gmin;
    max = gmax;
  } else {
    min = my_min;
    max = my_max;
  }
  return 0;
}

/** Convert from `int` to PETSc's `NormType`.
 * 
 *
 * @param[in] input norm type as an integer
 *
 * @return norm type as PETSc's `NormType`.
 */
NormType IceModelVec::int_to_normtype(int input) const {
  assert(input == NORM_1 || input == NORM_2 || input == NORM_INFINITY);

  switch (input) {
  case NORM_1:
    return NORM_1;
  case NORM_2:
    return NORM_2;
  default:
  case NORM_INFINITY:
    return NORM_INFINITY;
  }
}

//! Computes the norm of an IceModelVec by calling PETSc VecNorm.
/*!
See comment for range(); because local Vecs are VECSEQ, needs a reduce operation.
See src/trypetsc/localVecMax.c.

D@\note This method works for all IceModelVecs, including ones with
dof > 1. You might want to use norm_all() for IceModelVec2Stag,
though.
 */
PetscErrorCode IceModelVec::norm(int n, double &out) const {
  PetscErrorCode ierr;
  double my_norm, gnorm;
  assert(m_v != NULL);

  NormType type = int_to_normtype(n);

  ierr = VecNorm(m_v, type, &my_norm); CHKERRQ(ierr);

  if (m_has_ghosts == true) {
    // needs a reduce operation; use PISMGlobalMax if NORM_INFINITY,
    //   otherwise PISMGlobalSum; carefully in NORM_2 case
    if (n == NORM_1_AND_2) {
      SETERRQ1(grid->com, 1,
         "IceModelVec::norm(...): NORM_1_AND_2 not implemented (called as %s.norm(...))\n",
         m_name.c_str());
    } else if (n == NORM_1) {
      ierr = PISMGlobalSum(&my_norm, &gnorm, grid->com); CHKERRQ(ierr);
    } else if (n == NORM_2) {
      my_norm = PetscSqr(my_norm);  // undo sqrt in VecNorm before sum
      ierr = PISMGlobalSum(&my_norm, &gnorm, grid->com); CHKERRQ(ierr);
      gnorm = sqrt(gnorm);
    } else if (n == NORM_INFINITY) {
      ierr = PISMGlobalMax(&my_norm, &gnorm, grid->com); CHKERRQ(ierr);
    } else {
      SETERRQ1(grid->com, 2, "IceModelVec::norm(...): unknown norm type (called as %s.norm(...))\n",
         m_name.c_str());
    }
    out = gnorm;
  } else {
    out = my_norm;
  }
  return 0;
}

//! Result: v <- sqrt(v), elementwise.  Calls VecSqrt(v).
/*!
Name avoids clash with sqrt() in math.h.
 */
PetscErrorCode IceModelVec::squareroot() {
  PetscErrorCode ierr;
  assert(m_v != NULL);

  ierr = VecSqrtAbs(m_v); CHKERRQ(ierr);
  return 0;
}


//! Result: v <- v + alpha * x. Calls VecAXPY.
PetscErrorCode IceModelVec::add(double alpha, IceModelVec &x) {
  assert(m_v != NULL && x.m_v != NULL);

  PetscErrorCode ierr = checkCompatibility("add", x); CHKERRQ(ierr);

  ierr = VecAXPY(m_v, alpha, x.m_v); CHKERRQ(ierr);
  return 0;
}

//! Result: v[j] <- v[j] + alpha for all j. Calls VecShift.
PetscErrorCode IceModelVec::shift(double alpha) {
  assert(m_v != NULL);

  PetscErrorCode ierr = VecShift(m_v, alpha); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- v * alpha. Calls VecScale.
PetscErrorCode IceModelVec::scale(double alpha) {
  assert(m_v != NULL);

  PetscErrorCode ierr = VecScale(m_v, alpha); CHKERRQ(ierr);
  return 0;
}

//! Copies v to a global vector 'destination'. Ghost points are discarded.
/*! This is potentially dangerous: make sure that `destination` has the same
    dimensions as the current IceModelVec.

    DMLocalToGlobalBegin/End is broken in PETSc 3.5, so we roll our
    own.
 */
PetscErrorCode  IceModelVec::copy_to_vec(DM destination_da, Vec destination) const {
  PetscErrorCode ierr;
  assert(m_v != NULL);

  // m_dof > 1 for vector, staggered grid 2D fields, etc. In this case
  // m_n_levels == 1. For 3D fields, m_dof == 1 (all 3D fields are
  // scalar) and m_n_levels corresponds to dof of the underlying PETSc
  // DM object. So we want the bigger of the two numbers here.
  unsigned int N = std::max(m_dof, m_n_levels);

  ierr = this->get_dof(destination_da, destination, 0, N); CHKERRQ(ierr);

  return 0;
}

//! \brief Copies data from a Vec `source` to this IceModelVec. Updates ghost
//! points if necessary.
/*!
  Unlike DMLocalToGlobalBegin/End, DMGlobalToLocalBegin/End is *not*
  broken in PETSc 3.5 (and ealier), so we can use it here.
 */
PetscErrorCode IceModelVec::copy_from_vec(Vec source) {
  PetscErrorCode ierr;
  assert(m_v != NULL);

  if (m_has_ghosts) {
    ierr =   DMGlobalToLocalBegin(m_da, source, INSERT_VALUES, m_v);  CHKERRQ(ierr);
    ierr =     DMGlobalToLocalEnd(m_da, source, INSERT_VALUES, m_v);  CHKERRQ(ierr);
  } else {
    ierr = VecCopy(source, m_v); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode IceModelVec::get_dof(DM da_result, Vec result,
                                    unsigned int start, unsigned int count) const {
  PetscErrorCode ierr;
  void *tmp_res = NULL, *tmp_v = NULL;

  if (start >= m_dof)
    SETERRQ(grid->com, 1, "invalid argument (start)");

  ierr = DMDAVecGetArrayDOF(da_result, result, &tmp_res); CHKERRQ(ierr);
  double ***result_a = static_cast<double***>(tmp_res);

  ierr = DMDAVecGetArrayDOF(m_da, m_v, &tmp_v); CHKERRQ(ierr);
  double ***source_a = static_cast<double***>(tmp_v);

  for (int i=grid->xs; i<grid->xs+grid->xm; ++i) {
    for (int j=grid->ys; j<grid->ys+grid->ym; ++j) {
      ierr = PetscMemcpy(result_a[i][j], &source_a[i][j][start],
                         count*sizeof(PetscScalar)); CHKERRQ(ierr);
    }
  }
  
  ierr = DMDAVecRestoreArray(da_result, result, &tmp_res); CHKERRQ(ierr);
  ierr = DMDAVecRestoreArrayDOF(m_da, m_v, &tmp_v); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IceModelVec::set_dof(DM da_source, Vec source,
                                    unsigned int start, unsigned int count) {
  PetscErrorCode ierr;
  void *tmp_src = NULL, *tmp_v = NULL;

  if (start >= m_dof)
    SETERRQ(grid->com, 1, "invalid argument (start)");

  ierr = DMDAVecGetArrayDOF(da_source, source, &tmp_src); CHKERRQ(ierr);
  double ***source_a = static_cast<double***>(tmp_src);

  ierr = DMDAVecGetArrayDOF(m_da, m_v, &tmp_v); CHKERRQ(ierr);
  double ***result_a = static_cast<double***>(tmp_v);

  for (int i=grid->xs; i<grid->xs+grid->xm; ++i) {
    for (int j=grid->ys; j<grid->ys+grid->ym; ++j) {
      ierr = PetscMemcpy(&result_a[i][j][start], source_a[i][j],
                         count*sizeof(PetscScalar)); CHKERRQ(ierr);
    }
  }
  ierr = DMDAVecRestoreArray(da_source, source, &tmp_src); CHKERRQ(ierr);
  ierr = DMDAVecRestoreArrayDOF(m_da, m_v, &tmp_v); CHKERRQ(ierr);

  inc_state_counter();          // mark as modified

  return 0;
}

//! Result: destination <- v.  Leaves metadata alone but copies values in Vec.  Uses VecCopy.
PetscErrorCode  IceModelVec::copy_to(IceModelVec &destination) const {
  PetscErrorCode ierr;
  assert(m_v != NULL && destination.m_v != NULL);

  ierr = checkCompatibility("copy_to", destination); CHKERRQ(ierr);

  ierr = VecCopy(m_v, destination.m_v); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- source.  Leaves metadata alone but copies values in Vec.  Uses VecCopy.
PetscErrorCode  IceModelVec::copy_from(const IceModelVec &source) {
  PetscErrorCode ierr;
  assert(m_v != NULL && source.m_v != NULL);

  ierr = checkCompatibility("copy_from", source); CHKERRQ(ierr);

  ierr = VecCopy(source.m_v, m_v); CHKERRQ(ierr);
  return 0;
}

Vec IceModelVec::get_vec() {
  return m_v;
}

DM IceModelVec::get_dm() const {
  return m_da;
}

//! Sets the variable name to `name` and resets metadata.
PetscErrorCode  IceModelVec::set_name(std::string new_name, int N) {
  reset_attrs(N);

  if (N == 0)
    m_name = new_name;

  metadata(N).set_name(new_name);

  return 0;
}

std::string IceModelVec::name() const {
  return m_name;
}

//! Sets the variable's various names without changing any other metadata
PetscErrorCode IceModelVec::rename(std::string short_name, std::string long_name,
                                   std::string standard_name, int N) {

  if(short_name.empty() == false) {
    if (N == 0) m_name = short_name;
    metadata(N).set_name(short_name);
  }

  if (long_name.empty() == false) {
    metadata(N).set_string("long_name", long_name);
  }

  if (!standard_name.empty()) {
    metadata(N).set_string("standard_name", standard_name);
  }

  return 0;
}

//! Sets the glaciological units of an IceModelVec.
/*!
This affects NCVariable::report_range() and IceModelVec::write().  In write(),
if IceModelVec::write_in_glaciological_units == true, then that variable is written
with a conversion to the glaciological units set here.
 */
PetscErrorCode  IceModelVec::set_glaciological_units(std::string my_units) {

  PetscErrorCode ierr;

  for (unsigned int j = 0; j < m_dof; ++j) {
   ierr = metadata(j).set_glaciological_units(my_units); CHKERRQ(ierr);
  }

  return 0;
}

//! Resets most IceModelVec attributes.
PetscErrorCode IceModelVec::reset_attrs(unsigned int N) {

  write_in_glaciological_units = false;
  m_report_range                 = true;

  if (N > 0 && N < m_metadata.size()) {
    metadata(N).clear_all_strings();
    metadata(N).clear_all_doubles();
  }

  return 0;
}

//! Sets NetCDF attributes of an IceModelVec object.
/*! Call set_attrs("new pism_intent", "new long name", "new units", "") if a
  variable does not have a standard name. Similarly, by putting "" in an
  appropriate spot, it is possible tp leave long_name, units or pism_intent
  unmodified.

  If my_units != "", this also resets glaciological_units, so that they match
  internal units.
 */
PetscErrorCode IceModelVec::set_attrs(std::string my_pism_intent,
                                      std::string my_long_name,
                                      std::string my_units,
                                      std::string my_standard_name,
                                      int N) {

  metadata(N).set_string("long_name", my_long_name);

  PetscErrorCode ierr = metadata(N).set_units(my_units); CHKERRQ(ierr);

  metadata(N).set_string("pism_intent", my_pism_intent);

  metadata(N).set_string("standard_name", my_standard_name);

  return 0;
}


//! Gets an IceModelVec from a file `nc`, interpolating onto the current grid.
/*! Stops if the variable was not found and `critical` == true.
 */
PetscErrorCode IceModelVec::regrid(const PIO &nc, RegriddingFlag flag,
                                   double default_value) {
  PetscErrorCode ierr;
  Vec tmp;

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(grid->com, "  Regridding %s...\n", m_name.c_str()); CHKERRQ(ierr);
  }

  if (m_dof != 1)
    SETERRQ(grid->com, 1, "This method only supports IceModelVecs with dof == 1.");

  if (m_has_ghosts) {
    ierr = DMGetGlobalVector(m_da, &tmp); CHKERRQ(ierr);

    ierr = metadata(0).regrid(nc, flag, m_report_range, default_value, tmp); CHKERRQ(ierr);

    ierr = DMGlobalToLocalBegin(m_da, tmp, INSERT_VALUES, m_v); CHKERRQ(ierr);
    ierr = DMGlobalToLocalEnd(m_da, tmp, INSERT_VALUES, m_v); CHKERRQ(ierr);

    ierr = DMRestoreGlobalVector(m_da, &tmp); CHKERRQ(ierr);
  } else {
    ierr = metadata(0).regrid(nc, flag, m_report_range, default_value, m_v); CHKERRQ(ierr);
  }

  return 0;
}

//! Reads appropriate NetCDF variable(s) into an IceModelVec.
PetscErrorCode IceModelVec::read(const PIO &nc, const unsigned int time) {
  PetscErrorCode ierr;
  Vec tmp;

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(grid->com, "  Reading %s...\n", m_name.c_str()); CHKERRQ(ierr);
  }

  if (m_dof != 1)
    SETERRQ(grid->com, 1, "This method only supports IceModelVecs with dof == 1.");

  if (m_has_ghosts) {
    ierr = DMGetGlobalVector(m_da, &tmp); CHKERRQ(ierr);

    ierr = metadata(0).read(nc, time, tmp); CHKERRQ(ierr);

    ierr = DMGlobalToLocalBegin(m_da, tmp, INSERT_VALUES, m_v); CHKERRQ(ierr);
    ierr = DMGlobalToLocalEnd(m_da, tmp, INSERT_VALUES, m_v); CHKERRQ(ierr);

    ierr = DMRestoreGlobalVector(m_da, &tmp); CHKERRQ(ierr);
  } else {
    ierr = metadata(0).read(nc, time, m_v); CHKERRQ(ierr);
  }

  return 0;
}

//! \brief Define variables corresponding to an IceModelVec in a file opened using `nc`.
PetscErrorCode IceModelVec::define(const PIO &nc, PISM_IO_Type output_datatype) {
  PetscErrorCode ierr;

  for (unsigned int j = 0; j < m_dof; ++j) {
    ierr = metadata(j).define(nc, output_datatype, write_in_glaciological_units); CHKERRQ(ierr);
  }

  return 0;
}

//! \brief Read attributes from the corresponding variable in `nc`.
/*! Note that unlike read() and regrid(), this method does not use the standard
  name to find the variable to read attributes from.
 */
PetscErrorCode IceModelVec::read_attributes(std::string filename, int N) {
  PIO nc(*grid, "netcdf3");     // OK to use netcdf3
  PetscErrorCode ierr;

  ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);

  ierr = nc.read_attributes(metadata(N).get_name(),
                            metadata(N)); CHKERRQ(ierr);

  ierr = nc.close(); CHKERRQ(ierr);

  return 0;
}


//! @brief Returns a reference to the NCSpatialVariable object
//! containing metadata for the compoment N.
NCSpatialVariable& IceModelVec::metadata(unsigned int N) {
  return m_metadata[N];
}

//! Writes an IceModelVec to a NetCDF file.
PetscErrorCode IceModelVec::write(const PIO &nc, PISM_IO_Type nctype) {
  PetscErrorCode ierr;
  Vec tmp;

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(grid->com, "  Writing %s...\n", m_name.c_str()); CHKERRQ(ierr);
  }

  if (m_dof != 1)
    SETERRQ(grid->com, 1, "This method only supports IceModelVecs with dof == 1");

  if (m_has_ghosts) {
    ierr = DMGetGlobalVector(m_da, &tmp); CHKERRQ(ierr);

    ierr = this->copy_to_vec(m_da, tmp); CHKERRQ(ierr);

    ierr = metadata(0).write(nc, nctype, write_in_glaciological_units, tmp); CHKERRQ(ierr);

    ierr = DMRestoreGlobalVector(m_da, &tmp); CHKERRQ(ierr);
  } else {
    ierr = metadata(0).write(nc, nctype, write_in_glaciological_units, m_v); CHKERRQ(ierr);
  }

  return 0;
}

//! Dumps a variable to a file, overwriting this file's contents (for debugging).
PetscErrorCode IceModelVec::dump(const char filename[]) {
  PetscErrorCode ierr;
  PIO nc(*grid, grid->config.get_string("output_format"));

  // append = false, check_dimensions = true
  ierr = nc.open(filename, PISM_WRITE); CHKERRQ(ierr);
  ierr = nc.def_time(grid->config.get_string("time_dimension_name"),
                     grid->time->calendar(),
                     grid->time->units_string()); CHKERRQ(ierr);
  ierr = nc.append_time(grid->config.get_string("time_dimension_name"),
                        grid->time->current()); CHKERRQ(ierr);

  ierr = write(nc, PISM_DOUBLE); CHKERRQ(ierr);

  ierr = nc.close(); CHKERRQ(ierr);

  return 0;
}

//! Checks if two IceModelVecs have compatible sizes, dimensions and numbers of degrees of freedom.
PetscErrorCode IceModelVec::checkCompatibility(const char* func, const IceModelVec &other) const {
  PetscErrorCode ierr;
  int X_size, Y_size;

  if (m_dof != other.m_dof) {
    SETERRQ1(grid->com, 1, "IceModelVec::%s(...): operands have different numbers of degrees of freedom",
             func);
  }

  ierr = VecGetSize(m_v, &X_size); CHKERRQ(ierr);
  ierr = VecGetSize(other.m_v, &Y_size); CHKERRQ(ierr);
  if (X_size != Y_size) {
    SETERRQ4(grid->com, 1, "IceModelVec::%s(...): incompatible Vec sizes (called as %s.%s(%s))\n",
             func, m_name.c_str(), func, other.m_name.c_str());
  }


  return 0;
}

//! Checks if an IceModelVec is allocated and calls DAVecGetArray.
PetscErrorCode  IceModelVec::begin_access() const {
  PetscErrorCode ierr;
#if (PISM_DEBUG==1)
  assert(m_v != NULL);

  if (access_counter < 0)
    SETERRQ(grid->com, 1, "IceModelVec::begin_access(): access_counter < 0");
#endif

  if (access_counter == 0) {

    if (begin_end_access_use_dof == true) {
      ierr = DMDAVecGetArrayDOF(m_da, m_v, &array); CHKERRQ(ierr);
    } else {
      ierr = DMDAVecGetArray(m_da, m_v, &array); CHKERRQ(ierr);
    }
  }

  access_counter++;

  return 0;
}

//! Checks if an IceModelVec is allocated and calls DAVecRestoreArray.
PetscErrorCode  IceModelVec::end_access() const {
  PetscErrorCode ierr;
#if (PISM_DEBUG==1)
  assert(m_v != NULL);

  if (array == NULL)
    SETERRQ(grid->com, 1, "IceModelVec::end_access(): a == NULL (looks like begin_acces() was not called)");

  if (access_counter < 0)
    SETERRQ(grid->com, 1, "IceModelVec::end_access(): access_counter < 0");
#endif

  access_counter--;
  if (access_counter == 0) {
    if (begin_end_access_use_dof == true) {
      ierr = DMDAVecRestoreArrayDOF(m_da, m_v, &array);
      CHKERRQ(ierr);
    } else {
      ierr = DMDAVecRestoreArray(m_da, m_v, &array); CHKERRQ(ierr);
    }
    array = NULL;
  }

  return 0;
}

//! Updates ghost points.
PetscErrorCode  IceModelVec::update_ghosts() {
  PetscErrorCode ierr;
  if (m_has_ghosts == false)
    return 0;

  assert(m_v != NULL);
#if PETSC_VERSION_LT(3,5,0)
  ierr = DMDALocalToLocalBegin(m_da, m_v, INSERT_VALUES, m_v);  CHKERRQ(ierr);
  ierr = DMDALocalToLocalEnd(m_da, m_v, INSERT_VALUES, m_v); CHKERRQ(ierr);
#else
  ierr = DMLocalToLocalBegin(m_da, m_v, INSERT_VALUES, m_v);  CHKERRQ(ierr);
  ierr = DMLocalToLocalEnd(m_da, m_v, INSERT_VALUES, m_v); CHKERRQ(ierr);
#endif
  return 0;
}

//! Scatters ghost points to IceModelVec destination.
PetscErrorCode  IceModelVec::update_ghosts(IceModelVec &destination) {
  PetscErrorCode ierr;

  assert(m_v != NULL);

  if (m_has_ghosts == true && destination.m_has_ghosts == true) {
#if PETSC_VERSION_LT(3,5,0)
    ierr = DMDALocalToLocalBegin(m_da, m_v, INSERT_VALUES, destination.m_v);  CHKERRQ(ierr);
    ierr = DMDALocalToLocalEnd(m_da, m_v, INSERT_VALUES, destination.m_v);  CHKERRQ(ierr);
#else
    ierr = DMLocalToLocalBegin(m_da, m_v, INSERT_VALUES, destination.m_v);  CHKERRQ(ierr);
    ierr = DMLocalToLocalEnd(m_da, m_v, INSERT_VALUES, destination.m_v);  CHKERRQ(ierr);
#endif
    return 0;
  }

  if (m_has_ghosts == true && destination.m_has_ghosts == false) {
    ierr = this->copy_to_vec(destination.m_da, destination.m_v); CHKERRQ(ierr);
    return 0;
  }

  if (m_has_ghosts == false && destination.m_has_ghosts == true) {
    ierr = DMGlobalToLocalBegin(destination.m_da, m_v, INSERT_VALUES, destination.m_v);  CHKERRQ(ierr);
    ierr = DMGlobalToLocalEnd(destination.m_da, m_v, INSERT_VALUES, destination.m_v);  CHKERRQ(ierr);
    return 0;
  }

  if (m_has_ghosts == false && destination.m_has_ghosts == false) {
    SETERRQ2(grid->com, 1, "makes no sense to communicate ghosts for two GLOBAL IceModelVecs!"
             " (name1='%s', name2='%s')", m_name.c_str(), destination.m_name.c_str());
  }

  return 0;
}

//! Result: v[j] <- c for all j.
PetscErrorCode  IceModelVec::set(const double c) {
  PetscErrorCode ierr;
  assert(m_v != NULL);
  ierr = VecSet(m_v,c); CHKERRQ(ierr);
  return 0;
}

//! Checks if the current IceModelVec has NANs and reports if it does.
/*! Both prints and error message at stdout and returns nonzero. */
PetscErrorCode IceModelVec::has_nan() const {
  PetscErrorCode ierr;
  double tmp;

  ierr = norm(NORM_INFINITY, tmp); CHKERRQ(ierr);

  if ( gsl_isnan(tmp) ) {
    PetscPrintf(grid->com, "IceModelVec %s has uninitialized grid points (or NANs)\n", m_name.c_str());
    return 1;
  }

  return 0;
}

void IceModelVec::check_array_indices(int i, int j, unsigned int k) const {
  double ghost_width = 0;
  if (m_has_ghosts) ghost_width = m_da_stencil_width;
  bool out_of_range = (i < grid->xs - ghost_width) ||
    (i > grid->xs + grid->xm + ghost_width) ||
    (j < grid->ys - ghost_width) ||
    (j > grid->ys + grid->ym + ghost_width) ||
    (k >= m_dof);

  assert(out_of_range == false);
}

//! \brief Compute parameters for 2D loop computations involving 3
//! IceModelVecs.
/*!
 * Here we assume that z is updated using a local (point-wise) computation
 * involving x and y.
 *
 * "ghosts" is the width of the stencil that can be updated locally.
 * "scatter" is false if all ghosts can be updated locally.
 */
void compute_params(const IceModelVec* const x, const IceModelVec* const y,
                    const IceModelVec* const z, int &ghosts, bool &scatter) {

  // We have 2^3=8 cases here (x,y,z having or not having ghosts).
  if (z->has_ghosts() == false) {
    // z has no ghosts; we can update everything locally
    // (This covers 4 cases.)
    ghosts = 0;
    scatter = false;
  } else if (x->has_ghosts() == false ||
             y->has_ghosts() == false) {
    // z has ghosts, but at least one of x and y does not. we have to scatter
    // ghosts.
    // (This covers 3 cases.)
    ghosts = 0;
    scatter = true;
  } else {
    // all of x, y, z have ghosts
    // (The remaining 8-th case.)
    if (z->get_stencil_width() <= x->get_stencil_width() &&
        z->get_stencil_width() <= y->get_stencil_width()) {
      // x and y have enough ghosts to update ghosts of z locally
      ghosts = z->get_stencil_width();
      scatter = false;
    } else {
      // z has ghosts, but at least one of x and y doesn't have a wide enough
      // stencil
      ghosts = 0;
      scatter = true;
    }
  }
}

//! \brief Computes the norm of all components.
PetscErrorCode IceModelVec::norm_all(int n, std::vector<double> &result) {
  PetscErrorCode ierr;
  double *norm_result;

  assert(n == NORM_1 || n == NORM_2 || n == NORM_INFINITY);

  norm_result = new double[m_dof];
  result.resize(m_dof);

  NormType type = this->int_to_normtype(n);

  ierr = VecStrideNormAll(m_v, type, norm_result); CHKERRQ(ierr);

  if (m_has_ghosts) {
    // needs a reduce operation; use PISMGlobalMax if NORM_INFINITY,
    //   otherwise PISMGlobalSum; carefully in NORM_2 case
    if (n == NORM_1_AND_2) {
      SETERRQ1(grid->com, 1, 
         "IceModelVec::norm_all(...): NORM_1_AND_2 not implemented (called as %s.norm_all(...))\n",
         m_name.c_str());
    } else if (n == NORM_1) {

      for (unsigned int k = 0; k < m_dof; ++k) {
        ierr = PISMGlobalSum(&norm_result[k], &result[k], grid->com); CHKERRQ(ierr);
      }

    } else if (n == NORM_2) {

      for (unsigned int k = 0; k < m_dof; ++k) {
        norm_result[k] = PetscSqr(norm_result[k]);  // undo sqrt in VecNorm before sum
        ierr = PISMGlobalSum(&norm_result[k], &result[k], grid->com); CHKERRQ(ierr);
        result[k] = sqrt(result[k]);
      }

    } else if (n == NORM_INFINITY) {
      for (unsigned int k = 0; k < m_dof; ++k) {
        ierr = PISMGlobalMax(&norm_result[k], &result[k], grid->com); CHKERRQ(ierr);
      }
    } else {
      SETERRQ1(grid->com, 2, "IceModelVec::norm_all(...): unknown norm type (called as %s.norm_all(...))\n",
         m_name.c_str());
    }
  } else {

    for (unsigned int k = 0; k < m_dof; ++k) {
      result[k] = norm_result[k];
    }

  }

  delete [] norm_result;

  return 0;
}

PetscErrorCode IceModelVec::write(std::string filename, PISM_IO_Type nctype) {
  PetscErrorCode ierr;

  PIO nc(*grid, grid->config.get_string("output_format"));

  ierr = nc.open(filename, PISM_WRITE, true); CHKERRQ(ierr);

  ierr = this->write(nc, nctype); CHKERRQ(ierr);

  ierr = nc.close(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IceModelVec::read(std::string filename, unsigned int time) {
  PetscErrorCode ierr;

  PIO nc(*grid, "guess_mode");

  ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);

  ierr = this->read(nc, time); CHKERRQ(ierr);

  ierr = nc.close(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IceModelVec::regrid(std::string filename, RegriddingFlag flag,
                                   double default_value) {
  PetscErrorCode ierr;

  PIO nc(*grid, "guess_mode");

  ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);

  ierr = this->regrid(nc, flag, default_value); CHKERRQ(ierr);

  ierr = nc.close(); CHKERRQ(ierr);

  return 0;
}
