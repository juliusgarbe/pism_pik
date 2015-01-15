// Copyright (C) 2009--2014 Constantine Khroulev
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

#ifndef __NCVariable_hh
#define __NCVariable_hh

#include <set>
#include <map>
#include <vector>
#include <string>
#include <petscsys.h>
#include "PISMUnits.hh"
#include "PIO.hh"

class PISMTime;

//! @brief A class for handling variable metadata, reading, writing and converting
//! from input units and to output units.
/*! A NetCDF variable can have any number of attributes, but some of them get
    special treatment:

  - units: specifies internal units. When read, a variable is
    converted to these units. When written, it is converted from these
    to glaciological_units if write_in_glaciological_units is true.

  - glaciological_units: is never written to a file; replaces 'units'
    in the output if write_in_glaciological_units is true.

  - valid_min, valid_max: specify the valid range of a variable. Are
    read from an input file *only* if not specified previously. If
    both are set, then valid_range is used in the output instead.

  Also:

  - empty string attributes are ignored (they are not written to the
    output; file and has_attribute("foo") returns false if "foo" is
    absent or equal to an empty string).

  Typical attributes stored here:

  - long_name
  - standard_name
  - pism_intent
  - units
  - glaciological_units (saved to files as "units")

  Use the `name` of "PISM_GLOBAL" to read and write global attributes.
  (See also PIO.)

*/

class NCVariable {
public:
  NCVariable(std::string name, PISMUnitSystem system, unsigned int ndims = 0);
  virtual ~NCVariable();

  // setters
  PetscErrorCode set_units(std::string unit_spec);
  PetscErrorCode set_glaciological_units(std::string unit_spec);

  void set_double(std::string name, double value);
  void set_doubles(std::string name, std::vector<double> values);
  void set_name(std::string name);
  void set_string(std::string name, std::string value);

  void clear_all_doubles();
  void clear_all_strings();

  // getters
  PISMUnit get_units() const;
  PISMUnit get_glaciological_units() const;

  double get_double(std::string name) const;
  std::vector<double> get_doubles(std::string name) const;
  std::string get_name() const;
  std::string get_string(std::string name) const;

  unsigned int get_n_spatial_dimensions() const;

  bool has_attribute(std::string name) const;

  typedef std::map<std::string,std::string> StringAttrs;
  const StringAttrs& get_all_strings() const;

  typedef std::map<std::string,std::vector<double> > DoubleAttrs;
  const DoubleAttrs& get_all_doubles() const;

  PetscErrorCode report_to_stdout(MPI_Comm com, int verbosity_threshold) const;

protected:
  unsigned int m_n_spatial_dims;

private:
  PISMUnit m_units,                   //!< internal (PISM) units
    m_glaciological_units; //!< \brief for diagnostic variables: units
  //!< to use when writing to a NetCDF file and for standard out reports
  std::map<std::string, std::string> m_strings;  //!< string and boolean attributes
  std::map<std::string, std::vector<double> > m_doubles; //!< scalar and array attributes
  std::string m_short_name;
};

class LocalInterpCtx;
class IceGrid;

enum RegriddingFlag {OPTIONAL, CRITICAL};

//! Spatial NetCDF variable (corresponding to a 2D or 3D scalar field).
class NCSpatialVariable : public NCVariable {
public:
  NCSpatialVariable(PISMUnitSystem system);
  NCSpatialVariable(const NCSpatialVariable &other);
  virtual ~NCSpatialVariable();
  void init_2d(std::string name, IceGrid &g);
  void init_3d(std::string name, IceGrid &g, std::vector<double> &zlevels);
  void set_levels(const std::vector<double> &levels);

  void set_time_independent(bool flag);

  PetscErrorCode read(const PIO &file, unsigned int time, Vec v);
  PetscErrorCode write(const PIO &file, PISM_IO_Type nctype,
                       bool write_in_glaciological_units, Vec v);

  PetscErrorCode regrid(const PIO &file,
                        RegriddingFlag flag,
                        bool report_range,
                        double default_value, Vec v);
  PetscErrorCode regrid(const PIO &file,
                        unsigned int t_start,
                        RegriddingFlag flag,
                        bool report_range,
                        double default_value, Vec v);

  PetscErrorCode define(const PIO &nc, PISM_IO_Type nctype,
                        bool write_in_glaciological_units);

  NCVariable& get_x();
  NCVariable& get_y();
  NCVariable& get_z();

private:
  MPI_Comm m_com;
  std::string m_variable_order;        //!< variable order in output files;
  std::string m_time_dimension_name;
  NCVariable m_x, m_y, m_z;
  std::vector<double> m_zlevels;
  IceGrid *m_grid;
  PetscErrorCode report_range(Vec v, bool found_by_standard_name);
  PetscErrorCode check_range(std::string filename, Vec v);
  PetscErrorCode define_dimensions(const PIO &nc);
};

//! An internal class for reading, writing and converting time-series.
class NCTimeseries : public NCVariable {
public:
  NCTimeseries(std::string name, std::string dimension_name, PISMUnitSystem system);
  virtual ~NCTimeseries();

  std::string get_dimension_name() const;

  virtual PetscErrorCode define(const PIO &nc, PISM_IO_Type nctype, bool) const;
private:
  std::string m_dimension_name;        //!< the name of the NetCDF dimension this timeseries depends on
};

class NCTimeBounds : public NCTimeseries
{
public:
  NCTimeBounds(std::string name, std::string dimension_name, PISMUnitSystem system);
  virtual ~NCTimeBounds();
  virtual PetscErrorCode define(const PIO &nc, PISM_IO_Type nctype, bool) const;
private:
  std::string m_bounds_name;
};

#endif  // __NCVariable_hh
