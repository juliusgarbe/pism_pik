// Copyright (C) 2011, 2012, 2013, 2014 PISM Authors
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

#ifndef _PSCALARFORCING_H_
#define _PSCALARFORCING_H_

#include "IceGrid.hh"
#include "iceModelVec.hh"
#include "Timeseries.hh"
#include "pism_options.hh"
#include "PISMTime.hh"

template<class Model, class Mod>
class PScalarForcing : public Mod
{
public:
  PScalarForcing(IceGrid &g, const PISMConfig &conf, Model* in)
    : Mod(g, conf, in), input(in) {}
  virtual ~PScalarForcing()
  {
    if (offset)
      delete offset;
  }

  virtual PetscErrorCode update(double my_t, double my_dt)
  {
    Mod::m_t  = Mod::grid.time->mod(my_t - bc_reference_time, bc_period);
    Mod::m_dt = my_dt;

    PetscErrorCode ierr = Mod::input_model->update(my_t, my_dt); CHKERRQ(ierr);
    return 0;
  }

protected:
  virtual PetscErrorCode init_internal()
  {
    PetscErrorCode ierr;
    bool file_set, bc_period_set, bc_ref_year_set;

    IceGrid &g = Mod::grid;

    double bc_period_years = 0,
      bc_reference_year = 0;

    ierr = PetscOptionsBegin(g.com, "", "Scalar forcing options", ""); CHKERRQ(ierr);
    {
      ierr = PISMOptionsString(option_prefix + "_file", "Specifies a file with scalar offsets",
                               filename, file_set); CHKERRQ(ierr);
      ierr = PISMOptionsReal(option_prefix + "_period", "Specifies the length of the climate data period",
                             bc_period_years, bc_period_set); CHKERRQ(ierr);
      ierr = PISMOptionsReal(option_prefix + "_reference_year", "Boundary condition reference year",
                             bc_reference_year, bc_ref_year_set); CHKERRQ(ierr);
    }
    ierr = PetscOptionsEnd(); CHKERRQ(ierr);

    if (file_set == false) {
      ierr = PetscPrintf(g.com, "PISM ERROR: %s_file is not set.\n",
                         option_prefix.c_str()); CHKERRQ(ierr);
      PISMEnd();
    }

    if (bc_period_set) {
      bc_period = (unsigned int)bc_period_years;
    } else {
      bc_period = 0;
    }

    if (bc_ref_year_set) {
      bc_reference_time = g.convert(bc_reference_year, "years", "seconds");
    } else {
      bc_reference_time = 0;
    }


    ierr = verbPrintf(2, g.com,
                      "  reading %s data from forcing file %s...\n",
                      offset->short_name.c_str(), filename.c_str());
    CHKERRQ(ierr);
    PIO nc(g.com, "netcdf3", g.get_unit_system());
    ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);
    {
      ierr = offset->read(nc, g.time); CHKERRQ(ierr);
    }
    ierr = nc.close(); CHKERRQ(ierr);


    return 0;
  }

  PetscErrorCode offset_data(IceModelVec2S &result) {
    PetscErrorCode ierr = result.shift((*offset)(Mod::m_t + 0.5*Mod::m_dt)); CHKERRQ(ierr);
    return 0;
  }

  Model *input;
  Timeseries *offset;
  std::string filename, offset_name, option_prefix;

  unsigned int bc_period;       // in years
  double bc_reference_time;  // in seconds
};


#endif /* _PSCALARFORCING_H_ */
