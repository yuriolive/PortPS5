// libSceAvPlayer shares the native player implementation and its lifecycle/threading rules.
// Compile the C exports with APS5_VABI into this PRX so it has no native-PRX dependency.
#include "../libSceAvPlayer.native/Export.cpp"
