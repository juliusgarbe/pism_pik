// Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016 David Maxwell and Constantine Khroulev
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

%module(directors="1") cpp
%feature("autodoc", "2");

/* Don't warn about nested classes (325) and methods that can't be
 * wrapped under the present name, i.e. methods that require %rename.
 */
#pragma SWIG nowarn=325,503

%{
// The material in this section is included verbatim in the C++ source code generated by SWIG.
// The necessary header files required to compile must be included.
// This list is NOT the whole set of headers being wrapped; it is just the list of includes that
// draws in all the other needed includes as well. See the end of this file for the list
// of PISM headers being wrapped.

#include "base/util/interpolation.hh"

#include "base/util/pism_utilities.hh"

#include "base/util/PISMUnits.hh"
#include "pism_python.hh"

#include "base/grounded_cell_fraction.hh"
#include "base/util/Mask.hh"
#include "base/basalstrength/basal_resistance.hh"
#include "base/enthalpyConverter.hh"
#include "base/basalstrength/PISMMohrCoulombYieldStress.hh"
#include "base/util/error_handling.hh"
#include "base/util/PISMDiagnostic.hh"
#include "base/util/PISMConfig.hh"

#ifdef PISM_USE_JANSSON
#include "base/util/ConfigJSON.hh"
#endif

#include "base/util/MaxTimestep.hh"
#include "base/util/Context.hh"
#include "base/util/Logger.hh"
#include "base/util/Profiling.hh"

#include "base/util/projection.hh"
#include "base/energy/bootstrapping.hh"
%}

// Include petsc4py.i so that we get support for automatic handling of PetscErrorCode return values
%include "petsc4py/petsc4py.i"

%include "pism_exception.i"

// Automatic conversions between std::string and python string arguments and return values
%include std_string.i
// Conversions between python lists and certain STL vectors
%include std_vector.i
%include std_set.i
%include std_map.i

%include <std_shared_ptr.i>

%template(SizetVector) std::vector<size_t>;
%template(IntVector) std::vector<int>;
%template(UnsignedIntVector) std::vector<unsigned int>;
%template(DoubleVector) std::vector<double>;
%template(StringVector) std::vector<std::string>;
%template(StringSet) std::set<std::string>;
%template(DoubleVectorMap) std::map<std::string, std::vector<double> >;
%template(StringMap) std::map<std::string, std::string>;

// Why did I include this?
%include "cstring.i"

/* Type map for treating reference arguments as output. */
%define %Pism_reference_output_typemaps(TYPE)
%typemap(in, numinputs=0,noblock=1) TYPE & OUTPUT (TYPE temp) {
    $1 = &temp;
}
%typemap(argout,noblock=1) TYPE & OUTPUT
{
    %append_output(SWIG_NewPointerObj(%as_voidptr($1), $descriptor, 0 | %newpointer_flags));
};
%enddef

/* Tell SWIG that reference arguments are always output. */
%define %Pism_reference_is_always_output(TYPE)
%Pism_reference_output_typemaps(TYPE);
%apply TYPE & OUTPUT { TYPE &}
%enddef

%typemap(in, numinputs=0,noblock=1) bool & OUTPUT (bool temp = false) {
    $1 = &temp;
}

%typemap(argout,noblock=1) bool & OUTPUT
{
    %append_output(SWIG_From(bool)(*$1));
};

%typemap(in, numinputs=0,noblock=1) std::string& result (std::string temp) {
    $1 = &temp;
}

%typemap(in, numinputs=0,noblock=1) std::string& OUTPUT (std::string temp) {
    $1 = &temp;
}

%typemap(argout,noblock=1) std::string & OUTPUT
{
    %append_output(SWIG_FromCharPtr((*$1).c_str()));
}

%apply std::string &OUTPUT { std::string &result}

%typemap(in, numinputs=0,noblock=1) std::vector<double> & OUTPUT (std::vector<double> temp) {
    $1 = &temp;
}

%typemap(argout,noblock=1) std::vector<double> & OUTPUT
{
    int len;
    len = $1->size();
    $result = PyList_New(len);
     int i;
     for (i=0; i<len; i++) {
         PyList_SetItem($result, i, PyFloat_FromDouble((*$1)[i]));
     }
}

%apply std::vector<double> & OUTPUT {std::vector<double> &result};
%apply std::vector<std::string> & OUTPUT {std::vector<std::string> & result};

%apply int &OUTPUT {int &result};
%apply int *OUTPUT {int *out_mask};

%apply double & OUTPUT {double & result};
%apply double & OUTPUT {double & out};
%apply double * OUTPUT {double * result};
%apply bool & OUTPUT {bool & is_set, bool & result, bool & flag, bool & success};

// The SWIG built-in typecheck for a const char [] (used, e.g., with overloaded methods) checks that
// the string is zero length. So we have this bug fix from SWIG developer William Fulton here.
%typemap(typecheck,noblock=1,precedence=SWIG_TYPECHECK_STRING, fragment="SWIG_AsCharPtrAndSize") const char[] {
 int res = SWIG_AsCharPtrAndSize($input, 0, NULL, 0);
 $1 = SWIG_CheckState(res);
}


// Tell SWIG that the following variables are truly constant
%immutable pism::PISM_Revision;
%immutable pism::PISM_DefaultConfigFile;

/* PISM header with no dependence on other PISM headers. */
%include "base/util/pism_const.hh"
%include "base/util/pism_utilities.hh"
%include "base/util/interpolation.hh"

%shared_ptr(pism::Logger);
%shared_ptr(pism::StringLogger);
%include "base/util/Logger.hh"

%include pism_options.i

%ignore pism::Vector2::operator=;
%include "base/util/Vector2.hh"

%ignore pism::units::Unit::operator=;
%rename(UnitSystem) pism::units::System;
%rename(UnitConverter) pism::units::Converter;
%shared_ptr(pism::units::System);
%feature("valuewrapper") pism::units::System;
%feature("valuewrapper") pism::units::Unit;
%include "base/util/PISMUnits.hh"

%include pism_DM.i
%include pism_Vec.i
/* End of independent PISM classes. */

%shared_ptr(pism::Config);
%shared_ptr(pism::NetCDFConfig);
%shared_ptr(pism::DefaultConfig);
%include "base/util/PISMConfigInterface.hh"
%include "base/util/PISMConfig.hh"

#ifdef PISM_USE_JANSSON
%shared_ptr(pism::ConfigJSON);
%include "base/util/ConfigJSON.hh"
#endif

/* EnthalpyConverter uses Config, so we need to wrap Config first (see above). */
%shared_ptr(pism::EnthalpyConverter);
%shared_ptr(pism::ColdEnthalpyConverter);
%include "base/enthalpyConverter.hh"

%shared_ptr(pism::Time);
%include "base/util/PISMTime.hh"

%include "base/util/Profiling.hh"
%shared_ptr(pism::Context);
%include "base/util/Context.hh"

%include pism_IceGrid.i

/* PIO uses IceGrid, so IceGrid has to be wrapped first. */
%include pism_PIO.i

/* make sure PIO.i is included before VariableMetadata.hh */
%include pism_VariableMetadata.i

/* Timeseries uses IceGrid and VariableMetadata so they have to be wrapped first. */
%include pism_Timeseries.i

/* IceModelVec uses IceGrid and VariableMetadata so they have to be wrapped first. */
%include pism_IceModelVec.i

/* pism::Vars uses IceModelVec, so IceModelVec has to be wrapped first. */
%include pism_Vars.i

%include "base/util/PISMDiagnostic.hh"
%include "base/util/MaxTimestep.hh"
%include "base/util/PISMComponent.hh"
%include "base/basalstrength/basal_resistance.hh"

%include pism_FlowLaw.i

%include pism_ColumnSystem.i

/* SSAForwardRunFromInputFile sets up a yield stress model, which
 * requires a hydrology model.
 */
%include pism_Hydrology.i

%include "base/grounded_cell_fraction.hh"
%include "base/util/Mask.hh"
%include "pism_python.hh"
%include "base/basalstrength/PISMYieldStress.hh"
%include "base/basalstrength/PISMMohrCoulombYieldStress.hh"

%include pism_SSA.i

%include pism_SIA.i

%include pism_BedDef.i
/* The regional model implements some classes derived from SSAFD and
 * SIAFD, so this %include has to appear after %including the rest of
 * PISM's stress balance headers.
 */
%{
#include "regional/SSAFD_Regional.hh"
#include "regional/SIAFD_Regional.hh"
%}
%include "regional/SSAFD_Regional.hh"
%include "regional/SIAFD_Regional.hh"

%include "base/util/projection.hh"


%ignore pism::fem::q1::chi;
%ignore pism::fem::q1::n_chi;
%ignore pism::fem::q1::n_sides;
%ignore pism::fem::q1::incident_nodes;
%ignore pism::fem::p1::chi;
%ignore pism::fem::p1::n_sides;
%ignore pism::fem::p1::incident_nodes;
%include "base/stressbalance/ssa/FETools.hh"

%include pism_inverse.i

%include pism_ocean.i

%include pism_verification.i

%include "base/energy/bootstrapping.hh"
