On the vertical coordinate in PISM, and a critical change of variable {#vertchange}
===================================================================================
In PISM all fields in the ice, including enthalpy, age, velocity, and so on, evolve within an ice fluid domain of *changing* *geometry*.  See figure @ref freebdry.  In particular, the upper and lower surfaces of the ice fluid move with respect to the geoid.

@image html tempbdryfig.png "**freebdry**:  The ice fluid domain evolves, with both the upper and lower surfaces in motion with respect to the geoid.  FIXME: figure should show floating case too."
@anchor freebdry

The @f$ (x,y,z) @f$ coordinates in figure @ref freebdry are supposed to be from an orthogonal coordinate system with @f$ z @f$ in the direction anti-parallel to gravity, so this is a flat-earth approximation.  In practice, the data inputs to PISM are in some particular projection, of course.

We make a change of the independent variable @f$ z @f$ which simplifies how PISM deals with the changing geometry of the ice, especially in the cases of a non-flat or moving bed.  We replace the vertical coordinate relative to the geoid with the vertical coordinate relative to the base of the ice.  Let
    @f[ s = z - \begin{cases} b(x,y,t), & \text{ice is grounded}, \\
                              -\frac{\rho_i}{\rho_o} H(x,y,t), & \text{ice is floating; }
                                  \rho_i,\rho_o \text{ are densities of ocean, resp.,}
                \end{cases}@f]
where @f$ H = h - b @f$ is the ice thickness.

Now we make the change of variables
    @f[ (x,y,z,t) \mapsto (x,y,s,t) @f]
throughout the PISM code.  This replaces @f$ z=b @f$ by @f$ s=0 @f$ as the equation of the base surface of the ice.  The ice fluid domain in the new coordinates only has a free upper surface as shown in figure @ref sfreebdry.

@image html stempbdryfig.png "**sfreebdry**:  In (x,y,s) space the ice fluid domain only has an upper surface which moves, s=H(x,y,t).  Compare to figure freebdry.  FIXME: figure should show floating case too, and bedrock."
@anchor sfreebdry

In PISM the computational domain (region) @f$ \mathcal{R}=\left\{(x,y,s)\big| -L_x\le x \le L_x, -L_y\le y \le L_y, -Lb_z \le s \le L_z\right\} @f$ is divided into a three-dimensional grid.  See IceGrid.

The change of variable @f$ z\to s @f$ used here *is* *not* the [@ref Jenssen] change of variable @f$ \tilde s=(z-b)/H @f$ .  That change causes the conservation of energy equation to become singular at the boundaries of the ice sheet.  Specifically, the Jenssen change replaces the vertical conduction term by a manifestly-singular term at ice sheet margins where @f$ H\to 0 @f$ , because
   @f[ \frac{\partial^2 E}{\partial z^2} = \frac{1}{H^2} \frac{\partial^2 E}{\partial \tilde s^2}. @f]
A singular coefficient of this type can be assumed to affect the stability of all time-stepping schemes.  The current change @f$ s=z-b @f$ has no such singularizing effect though the change does result in added advection terms in the conservation of energy equation, which we now address.  See the page \ref bombproofenth for more general considerations about the conservation of energy equation.

The new coordinates @f$ (x,y,s) @f$ are not orthogonal.

Recall that if @f$ f=f(x,y,z,t) @f$ is a function written in the old variables and if @f$ \tilde f(x,y,s,t)=f(x,y,z(x,y,s,t),t) @f$ is the "same" function written in the new variables, equivalently @f$ f(x,y,z,t)=\tilde f(x,y,s(x,y,z,t),t) @f$ , then
    @f[ \frac{\partial f}{\partial x} = \frac{\partial \tilde f}{\partial x} + \frac{\partial \tilde f}{\partial s} \frac{\partial s}{\partial x} = \frac{\partial \tilde f}{\partial x} - \frac{\partial \tilde f}{\partial s} \frac{\partial b}{\partial x}. @f]
Similarly,
    @f[ \frac{\partial f}{\partial y} = \frac{\partial \tilde f}{\partial y} - \frac{\partial \tilde f}{\partial s} \frac{\partial b}{\partial y}, @f]
    @f[ \frac{\partial f}{\partial t} = \frac{\partial \tilde f}{\partial t} - \frac{\partial \tilde f}{\partial s} \frac{\partial b}{\partial t}. @f]
On the other hand,
    @f[ \frac{\partial f}{\partial z} = \frac{\partial \tilde f}{\partial s}. @f]

The following table records some important changes to formulae related to conservation of energy:
\f{align*}{
&\textbf{old} && \textbf{new} \\
&P=\rho g(h-z) && P=\rho g(H-s) \\
&\frac{\partial E}{\partial t} && \frac{\partial E}{\partial t}-\frac{\partial E}{\partial s}\frac{\partial b}{\partial t} \\
&\nabla E && \nabla E- \frac{\partial E}{\partial s}\nabla b \\
&\rho_i\left(\frac{\partial E}{\partial t}+\mathbf{U}\cdot\nabla E + w\frac{\partial E}{\partial z}\right)=\frac{k_i}{c_i} \frac{\partial^2 E}{\partial z^2} + Q && \rho_i\left(\frac{\partial E}{\partial t} + \mathbf{U}\cdot\nabla E + \left(w-\frac{\partial b}{\partial t}-\mathbf{U}\cdot\nabla b\right)\frac{\partial E}{\partial s}\right) = \frac{k_i}{c_i} \frac{\partial^2 E}{\partial s^2} + Q
\f}
Note @f$ E @f$ is the ice enthalpy and @f$ T @f$ is the ice temperature (which is a function of the enthalpy; see EnthalpyConverter), @f$ P @f$ is the ice pressure (assumed hydrostatic), @f$ \mathbf{U} @f$ is the depth-dependent horizontal velocity, and @f$ Q @f$ is the strain-heating term.

Now the vertical velocity is computed by IceModel::vertVelocityFromIncompressibility().  In the old coordinates @f$ (x,y,z,t) @f$ it has this formula:

    @f[ w(z) = -\int_b^z \frac{\partial u}{\partial x}(z') + \frac{\partial v}{\partial y}(z')\,dz' + \frac{\partial b}{\partial t} + \mathbf{U}_b \cdot \nabla b - S.@f]

Here @f$ S @f$ is the basal melt rate, positive when ice is being melted (= IceModel::vbmr).  We have used the basal kinematical equation and integrated the incompressibility statement

    @f[ \frac{\partial u}{\partial x} + \frac{\partial v}{\partial y} + \frac{\partial w}{\partial z} = 0.@f]

In the new coordinates we have

    @f[ w(s) = -\int_0^s \frac{\partial u}{\partial x}(s') + \frac{\partial v}{\partial y}(s')\,ds' + \mathbf{U}(s) \cdot \nabla b + \frac{\partial b}{\partial t} - S.@f]

(Note that the term @f$ \mathbf{U}(s) \cdot \nabla b @f$ evaluates the horizontal velocity at level @f$ s @f$ and not at the base.)

Let
     @f[ \tilde w(x,y,s,t) = w(s) - \frac{\partial b}{\partial t}-\mathbf{U}(s)\cdot\nabla b. @f]

This quantity is the vertical velocity of the ice <i>relative to the location on the bed immediately below it</i>.  In particular, @f$ \tilde w=0 @f$ for a slab sliding down a non-moving inclined plane at constant horizontal velocity, if there is no basal melt rate.  Also, @f$ \tilde w(s=0) @f$ is nonzero only if there is basal melting or freeze-on, %i.e. when @f$ S\ne 0 @f$ .  Within PISM, @f$ \tilde w @f$ is the IceModelVec3 with name IceModel::w3, and it is written with name `wvel_rel` into an input file.  Comparing the last two equations, we see how IceModel::vertVelocityFromIncompressibility() computes @f$ \tilde w @f$ :

    @f[ \tilde w(s) = -\int_0^s \frac{\partial u}{\partial x}(s') + \frac{\partial v}{\partial y}(s')\,ds' - S.@f]

The conservation of energy equation is now, in the new coordinate @f$ s @f$ and newly-defined relative vertical velocity,

    @f[ \rho_i \left(\frac{\partial E}{\partial t} + \mathbf{U}\cdot\nabla E + \tilde w \frac{\partial E}{\partial s}\right) = \frac{k_i}{c_i} \frac{\partial^2 E}{\partial s^2} + Q @f]

Thus it looks just like the conservation of energy equation in the original vertical velocity @f$ z @f$ .  This is the form of the equation solved by IceModel::enthalpyAndDrainageStep().

Under option `-o_size big`, all of these vertical velocity fields are available as fields in the output NetCDFfile.  The vertical velocity relative to the geoid, as a three-dimensional field, uis writtenas the diagnostic variable `wvel`.  This is the "actual" vertical velocity @f$ w = \tilde w + \frac{\partial b}{\partial t} + \mathbf{U}(s)\cdot\nabla b @f$ .  Its surface value is written as `wvelsurf`, and its basal value as `wvelbase`.  The relative vertical velocity @f$ \tilde w @f$ is written to the NetCDF output file as `wvel_rel`.
