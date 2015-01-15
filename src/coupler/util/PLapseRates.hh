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

#ifndef _PLAPSERATES_H_
#define _PLAPSERATES_H_

#include "IceGrid.hh"
#include "iceModelVec2T.hh"
#include "pism_options.hh"
#include "PIO.hh"
#include "PISMVars.hh"
#include "PISMTime.hh"
#include "PISMConfig.hh"
#include <assert.h>

template <class Model, class Mod>
class PLapseRates : public Mod
{
public:
  PLapseRates(IceGrid &g, const PISMConfig &conf, Model* in)
    : Mod(g, conf, in)
  {
    surface = thk = NULL;
    temp_lapse_rate = 0.0;
  }

  virtual ~PLapseRates() {}

  virtual PetscErrorCode update(double my_t, double my_dt)
  {
    PetscErrorCode ierr;

    // a convenience
    double &t = Mod::m_t;
    double &dt = Mod::m_dt;

    // "Periodize" the climate:
    my_t = Mod::grid.time->mod(my_t - bc_reference_time,  bc_period);

    if ((fabs(my_t - t) < 1e-12) &&
        (fabs(my_dt - dt) < 1e-12))
      return 0;

    t  = my_t;
    dt = my_dt;

    // NB! Input model uses original t and dt
    ierr = Mod::input_model->update(my_t, my_dt); CHKERRQ(ierr);

    ierr = reference_surface.update(t, dt); CHKERRQ(ierr);

    ierr = reference_surface.interp(t + 0.5*dt); CHKERRQ(ierr);

    return 0;
  }

  virtual PetscErrorCode max_timestep(double t, double &dt, bool &restrict) {
    PetscErrorCode ierr;
    double max_dt = -1;

    // "Periodize" the climate:
    t = Mod::grid.time->mod(t - bc_reference_time, bc_period);

    ierr = Mod::input_model->max_timestep(t, dt, restrict); CHKERRQ(ierr);

    max_dt = reference_surface.max_timestep(t);

    if (restrict == true) {
      if (max_dt > 0)
        dt = PetscMin(max_dt, dt);
    }
    else dt = max_dt;

    if (dt > 0)
      restrict = true;
    else
      restrict = false;

    return 0;
  }

protected:
  IceModelVec2T reference_surface;
  IceModelVec2S *surface, *thk;
  unsigned int bc_period;
  double bc_reference_time,          // in seconds
    temp_lapse_rate;
  std::string option_prefix;

  virtual PetscErrorCode init_internal(PISMVars &vars)
  {
    PetscErrorCode ierr;
    std::string filename;
    bool bc_file_set, bc_period_set, bc_ref_year_set, temp_lapse_rate_set;

    IceGrid &g = Mod::grid;

    double bc_period_years = 0,
      bc_reference_year = 0;

    ierr = PetscOptionsBegin(g.com, "", "Lapse rate options", ""); CHKERRQ(ierr);
    {
      ierr = PISMOptionsString(option_prefix + "_file",
                               "Specifies a file with top-surface boundary conditions",
                               filename, bc_file_set); CHKERRQ(ierr);
      ierr = PISMOptionsReal(option_prefix + "_period",
                             "Specifies the length of the climate data period",
                             bc_period_years, bc_period_set); CHKERRQ(ierr);
      ierr = PISMOptionsReal(option_prefix + "_reference_year",
                             "Boundary condition reference year",
                             bc_reference_year, bc_ref_year_set); CHKERRQ(ierr);
      ierr = PISMOptionsReal("-temp_lapse_rate",
                             "Elevation lapse rate for the temperature, in K per km",
                             temp_lapse_rate, temp_lapse_rate_set); CHKERRQ(ierr);
    }
    ierr = PetscOptionsEnd(); CHKERRQ(ierr);

    if (bc_file_set == false) {
      PetscPrintf(Model::grid.com, "PISM ERROR: option %s_file is required.\n", option_prefix.c_str());
      PISMEnd();
    }

    if (bc_ref_year_set) {
      bc_reference_time = Model::grid.convert(bc_reference_year, "years", "seconds");
    } else {
      bc_reference_time = 0;
    }

    if (bc_period_set) {
      bc_period = (unsigned int)bc_period_years;
    } else {
      bc_period = 0;
    }

    if (reference_surface.was_created() == false) {
      unsigned int buffer_size = (unsigned int) Mod::config.get("climate_forcing_buffer_size"),
        ref_surface_n_records = 1;

      PIO nc(g.com, "netcdf3", g.get_unit_system());
      ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);
      ierr = nc.inq_nrecords("usurf", "surface_altitude", ref_surface_n_records); CHKERRQ(ierr);
      ierr = nc.close(); CHKERRQ(ierr);

      // if -..._period is not set, make n_records the minimum of the
      // buffer size and the number of available records. Otherwise try
      // to keep all available records in memory.
      if (bc_period_set == false) {
        ref_surface_n_records = PetscMin(ref_surface_n_records, buffer_size);
      }

      if (ref_surface_n_records == 0) {
        PetscPrintf(g.com, "PISM ERROR: can't find reference surface elevation (usurf) in %s.\n",
                    filename.c_str());
        PISMEnd();
      }

      reference_surface.set_n_records(ref_surface_n_records);
      ierr = reference_surface.create(g, "usurf", false); CHKERRQ(ierr);
      ierr = reference_surface.set_attrs("climate_forcing",
                                         "reference surface for lapse rate corrections",
                                         "m", "surface_altitude"); CHKERRQ(ierr);
      reference_surface.set_n_evaluations_per_year((unsigned int)Mod::config.get("climate_forcing_evaluations_per_year"));
    }

    ierr = verbPrintf(2, g.com,
                      "    reading reference surface elevation from %s ...\n",
                      filename.c_str()); CHKERRQ(ierr);

    ierr = reference_surface.init(filename, bc_period, bc_reference_time); CHKERRQ(ierr);

    surface = dynamic_cast<IceModelVec2S*>(vars.get("surface_altitude"));
    assert(surface != NULL);

    thk = dynamic_cast<IceModelVec2S*>(vars.get("land_ice_thickness"));
    assert(thk != NULL);

    return 0;
  }

  PetscErrorCode lapse_rate_correction(IceModelVec2S &result, double lapse_rate)
  {
    PetscErrorCode ierr;

    if (PetscAbs(lapse_rate) < 1e-12)
      return 0;

    ierr = thk->begin_access(); CHKERRQ(ierr);
    ierr = surface->begin_access(); CHKERRQ(ierr);
    ierr = reference_surface.begin_access(); CHKERRQ(ierr);
    ierr = result.begin_access(); CHKERRQ(ierr);

    IceGrid &g = Mod::grid;

    for (int   i = g.xs; i < g.xs + g.xm; ++i) {
      for (int j = g.ys; j < g.ys + g.ym; ++j) {
        if ((*thk)(i,j) > 0) {
          const double correction = lapse_rate * ((*surface)(i,j) - reference_surface(i,j));
          result(i,j) -= correction;
        }
      }
    }

    ierr = result.end_access(); CHKERRQ(ierr);
    ierr = reference_surface.end_access(); CHKERRQ(ierr);
    ierr = surface->end_access(); CHKERRQ(ierr);
    ierr = thk->end_access(); CHKERRQ(ierr);

    return 0;
  }
};


#endif /* _PLAPSERATES_H_ */
