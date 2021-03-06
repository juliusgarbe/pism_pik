// Copyright (C) 2011, 2012, 2013, 2014, 2015 PISM Authors
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

#include "base/util/IceGrid.hh"
#include "base/util/PISMConfigInterface.hh"
#include "base/util/PISMTime.hh"
#include "base/util/Timeseries.hh"
#include "base/util/error_handling.hh"
#include "base/util/iceModelVec.hh"
#include "base/util/io/PIO.hh"
#include "base/util/pism_options.hh"

namespace pism {

template<class Model, class Mod>
class PScalarForcing : public Mod
{
public:
  PScalarForcing(IceGrid::ConstPtr g, Model* in)
    : Mod(g, in), input(in) {}

  virtual ~PScalarForcing()
  {
    if (offset) {
      delete offset;
    }
  }

protected:
  virtual void update_impl(double my_t, double my_dt)
  {
    Mod::m_t  = Mod::m_grid->ctx()->time()->mod(my_t - bc_reference_time, bc_period);
    Mod::m_dt = my_dt;

    Mod::input_model->update(my_t, my_dt);
  }

  virtual void init_internal()
  {
    IceGrid::ConstPtr g = Mod::m_grid;

    options::String file(option_prefix + "_file", "Specifies a file with scalar offsets");
    options::Integer period(option_prefix + "_period",
                            "Specifies the length of the climate data period", 0);
    options::Real bc_reference_year(option_prefix + "_reference_year",
                                    "Boundary condition reference year", 0.0);

    if (not file.is_set()) {
      throw RuntimeError::formatted("command-line option %s_file is required.",
                                    option_prefix.c_str());
    }

    if (period.value() < 0.0) {
      throw RuntimeError::formatted("invalid %s_period %d (period length cannot be negative)",
                                    option_prefix.c_str(), period.value());
    }
    bc_period = (unsigned int)period;

    if (bc_reference_year.is_set()) {
      bc_reference_time = units::convert(Mod::m_sys, bc_reference_year, "years", "seconds");
    } else {
      bc_reference_time = 0;
    }

    Mod::m_log->message(2,
               "  reading %s data from forcing file %s...\n",
               offset->short_name.c_str(), file->c_str());

    PIO nc(g->com, "netcdf3");
    nc.open(file, PISM_READONLY);
    {
      offset->read(nc, *g->ctx()->time(), *g->ctx()->log());
    }
    nc.close();
  }

  //! Apply offset as an offset
  void offset_data(IceModelVec2S &result) {
    result.shift((*offset)(Mod::m_t + 0.5*Mod::m_dt));
  }

  //! Apply offset as a scaling factor
  void scale_data(IceModelVec2S &result) {
    result.scale((*offset)(Mod::m_t + 0.5*Mod::m_dt));
  }

  Model *input;
  Timeseries *offset;
  std::string filename, offset_name, option_prefix;

  unsigned int bc_period;       // in years
  double bc_reference_time;  // in seconds
};


} // end of namespace pism

#endif /* _PSCALARFORCING_H_ */
