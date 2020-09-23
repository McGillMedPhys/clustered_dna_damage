//
// ********************************************************************
// *                                                                  *
// * This file is part of the TOPAS-nBio extensions to the            *
// *   TOPAS Simulation Toolkit.                                      *
// * The TOPAS-nBio extensions are freely available under the license *
// *   agreement set forth at: https://topas-nbio.readthedocs.io/     *
// *                                                                  *
// ********************************************************************
//

#ifndef TsFractalDNAV2_hh
#define TsFractalDNAV2_hh

#include "TsVGeometryComponent.hh"
#include "GeoManagerV2.hh"


class TsFractalDNAV2 : public TsVGeometryComponent
{    
public:
	TsFractalDNAV2(TsParameterManager* pM, TsExtensionManager* eM, TsMaterialManager* mM, TsGeometryManager* gM,
				  TsVGeometryComponent* parentComponent, G4VPhysicalVolume* parentVolume, G4String& name);
	~TsFractalDNAV2();
    
    GeoManagerV2* fGeoManager;
	
    void ResolveParameters();
	G4VPhysicalVolume* Construct();
    
    std::vector<G4ThreeVector> GetSugar1Info();
    
private:
    
    std::vector<G4VPhysicalVolume*> physFibers;
    
    std::vector<G4double> fx;
    std::vector<G4double> fy;
    std::vector<G4double> fz;
    
    // std::vector<G4Tubs*> sLoop; // Vector of solids containing cylindrical fibers
    std::vector<G4LogicalVolume*> lLoop; // Vector of logical volumes containing chromosome territories
    std::vector<G4LogicalVolume*> lmLoop; // Vector of logical volumes containing chromosome territories
    
    G4bool fBuildBases;
    
    G4int fMaxNumFibers;
    G4int fNumFibers;  
};

#endif
