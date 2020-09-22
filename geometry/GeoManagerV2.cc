// Extra Class for TsFiber
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

#include "GeoManagerV2.hh"

GeoManagerV2::GeoManagerV2(G4int verbose, G4double factor)
{
    fVerbose = verbose;
    fFactor = factor;

    geoCalculation = new GeoCalculationV2(fVerbose, fFactor);
    geoVolume = new GeoVolumeV2(fVerbose, fFactor);

}

GeoManagerV2::~GeoManagerV2()
{
    // clean the memory
    //
    delete geoCalculation;
    delete geoVolume;


}

void GeoManagerV2::Initialize()
{
    // do the calculations using GeoCalculation
    geoCalculation->Initialize();

    // Set the parameters of the GeoVolume class.
    // This must be done before calling any of the Build method in the class.
    //
    geoVolume->SetBaseRadius(geoCalculation->GetBaseRadius());
    geoVolume->SetBaseRadiusWater(geoCalculation->GetBaseRadiusWater());
    geoVolume->SetSugarTMPRadius(geoCalculation->GetSugarTMPRadius());
    geoVolume->SetSugarTHFRadius(geoCalculation->GetSugarTHFRadius());
    geoVolume->SetSugarTMPRadiusWater(geoCalculation->GetSugarTMPRadiusWater());
    geoVolume->SetSugarTHFRadiusWater(geoCalculation->GetSugarTHFRadiusWater());

    geoVolume->SetHistoneHeight(geoCalculation->GetHistoneHeight());
    geoVolume->SetHistoneRadius(geoCalculation->GetHistoneRadius());

    geoVolume->SetNucleoNum(90);
    geoVolume->SetBpNum(200);

    geoVolume->SetFiberPitch(geoCalculation->GetFiberPitch());
    geoVolume->SetFiberDeltaAngle(geoCalculation->GetFiberDeltaAngle());
    geoVolume->SetFiberNbNuclPerTurn(geoCalculation->GetFiberNbNuclPerTurn());
}

G4LogicalVolume* GeoManagerV2::BuildLogicFiber(G4bool isVisu)
{
    G4LogicalVolume* lFiber = geoVolume->BuildLogicFiber(geoCalculation->GetAllDNAVolumePositions(),
                                                             geoCalculation->GetNucleosomePosition(),
                                                             geoCalculation->GetPosAndRadiusMap(),
                                                             isVisu);

    return lFiber;
}


std::map<G4String, std::vector<std::vector<double> > > *GeoManagerV2::GetDNAMoleculesPositions()
{
    return geoVolume->GetDNAMoleculesPositions();
}
