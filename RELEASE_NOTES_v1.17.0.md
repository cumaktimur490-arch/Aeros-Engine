# Aeros Engine v1.17.0 — Physics Logic Fix

## Цель релиза
Исправление логики и использование данных о аэродинамике и физике — полный аудит физической корректности.

## Исправления по модулям

### flow_field.cpp v1.17.0
- **Эллипсоидный потенциал**: корректный rad-scaled Un, shapeFactor от elongation, правильная формула 0.5*(3(U·r̂)r̂ - U)/r³
- **Ground Effect**: зеркальное отражение с инверсией vy (no-penetration), Venturi с сохранением массы 1.6x max
- **Boundary Layer**: Re_x через Sutherland mu(T), Blasius/Schlichting
- **Separation**: критерий Stratford с проверкой flow·n
- **Wake**: сохранение импульса sqrt(D/x) Gaussian b∝sqrt(x)
- **Cd_est**: учет elongation + Re crisis
- **Strouhal**: St(Re) для Karman shedding
- **Turbulence**: divergence-free curl noise, поперечная компонента только
- **Сжимаемость**: Prandtl-Glauert beta=sqrt(1-M²)

### forces.cpp v1.17.0
- Frontal area 0.5|dot| (проекция)
- Base area: flowDotN>0.7 (только тыльная)
- Base drag условный avgCpBase>-0.05
- Cf: Blasius 0.664/sqrt(Re_x) ламинарный, Schlichting 0.455/log10(Re)^2.58 турбулентный + roughness
- Leading Edge: min along chord
- AC 25% хорды
- CoP pressure-weighted
- Ground downforce geFactor
- AR = span²/refArea реальный
- eOswald, Cd_i=Cl²/(πAR e), wave drag

### lbm.cpp v1.17.0
- L_LB = L/cellAvg clamped 5-200 (объект в клетках, не Nx)
- Inlet curl A divergence-free поперечный
- Convective outlet df/dt+U df/dx=0 convCoeff=U0*0.8 clamped 0.01-0.5 + 0.15 second-order term
- Equilibrium без жесткого clamp -0.5/2.0, только positiveness
- usqr 0.09, U0 0.15 limit
- Smagorinsky (Cs*dx)²|S| + van Driest damping у стенок
- tau 0.51-1.5, maxV 1.8x
- TKE = (Cs*dx*S)²

### particles.cpp v1.17.0
- Drag Fd=0.5 Cd rho A |Vrel|² физичный
- Cd сферы Whitaker: 24/Re Stokes, 24/Re*(1+0.15Re^0.687) <1, +0.42/(1+42500Re^-1.16) <1000, 0.44 <2e5 else 0.1
- Re_p rho|Vrel|d/mu Sutherland mu(T) T0 273.15 S110.4 mu0 1.716e-5
- Уравнение m*dv/dt=Fd+Fg+Fb, Fg rho_p V g, Fb rho_f V g
- tau_p rho_p d²/18mu, vTerm=tau_p g
- RK4 для pos+vel, SDF collision
- Size distribution 10-30µm lognormal
- Restitution normal 0.1 tangential friction 0.82, slide flowTang
- Sedimentation vTerm respawn

### streamlines.cpp v1.17.0
- RK45 error estimate |RK4-RK5|
- Adaptive dt ∝1/speed с reduction по curvature/vorticity
- Seeding cosine clustering sin(fx*π/2) 50/50 + LE rings radius 0.4maxDim + peripheral vortex 30%
- Termination: stagnationCounter>5, vortex trap vort>50 & low speed & qCrit>0, bigMargin 2.8maxDim
- Sliding на SDF с Stratford check flowDotN>0 separation break

### voxel_grid.cpp v1.17.0
- Trilinear SDF sampling fx=(x-min)/cs-0.5 tx clamp 8-corner lerp
- sdfNormal via eps central diff sampleSDFCPU fallback voxel central diff
- Euclidean SDF 26-neighbor weights w=sqrt(wx²+wy²+wz²)
- Fast Sweeping 4 sweeps ×8 dirs + Dijkstra BFS queue
- Gaussian 1D kernel [0.25,0.5,0.25] 2 iterations 0.35 blend surface preserve <1.2 cs

### atmosphere.cpp v1.17.0
- ISA 7 слоев с вычисляемыми pBase для непрерывности
- T_base, P_base последовательно по формулам
- Корректный g0, R, lapse, экспонента -g0/(L*R) и -g0*dh/(R*T)
- Density, pressure, temperature, speedOfSound с clamp

### framegen.cpp v1.17.0
- Motion vectors с аэродинамикой: ellipsoidPotential в шейдере, ground mirror vy inversion, Venturi, wake deficit + Karman
- Aero offset: worldPrev = worldCurr - vFluid*dt
- Depth occlusion check depthDiff>0.05
- Использует flowParams, Re, Mach, ground, wake, Strouhal
- Motion clamped 0.35, far plane check

## Данные аэродинамики использованы
- ISA 7 слоёв, Sutherland mu(T), плотность/давление/температура/speedOfSound
- Reynolds Re=rho*V*L/mu, Mach M=V/a
- Cp=(p-p_inf)/q и Bernoulli 1-(V/Vinf)² с Prandtl-Glauert beta=sqrt(1-M²)
- Drag polar Cd=Cd0+Cd_i+Cd_wave+Cd_base, Cd0 из Cf*wetArea/refArea
- Cf Blasius 0.664/sqrt(Re_x) ламинарный и Schlichting 0.455/log10(Re)^2.58 турбулентный
- Cd_i=Cl²/(πAR e), AR реальный span²/ref
- Base drag из геометрии flowDotN>0.7
- Ground effect Venturi с сохранением массы 1.6x
- Wake momentum deficit sqrt(D/x) Gaussian b∝sqrt(x)
- Strouhal St(Re) для Karman
- Boundary layer separation критерий Stratford
- LBM tau из физического Re, Smagorinsky LES (Cs*dx)²|S| van Driest
- Частицы Stokes number, Whitaker drag

## Версия
- 1.17.0 GoGonam AoS.
- app_icon.rc FILEVERSION 1,17,0,0
- version.h AEROS_VERSION_STRING "1.17.0"
- VERSION file 1.17.0
