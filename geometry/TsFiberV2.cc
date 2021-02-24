// Component for TsFiberV2
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
// Code that reads in the DNA fiber geometry from the DNAFabric software package tool
// More details about the fiber geometry can be found in:
// Meylan et al. (2016) Commputer Physics Comunications, 204, 159
// Any report or published results obtained using the Geant4-DNA software
// shall cite the following Geant4-DNA collaboration publication:


#include "TsFiberV2.hh"
#include "GeoManagerV2.hh"
#include "GeoCalculationV2.hh"

#include "TsParameterManager.hh"

#include "G4SystemOfUnits.hh"
#include "G4Region.hh"
#include "G4ProductionCuts.hh"
#include "G4UserLimits.hh"
#include "G4NistManager.hh"
#include "G4RunManager.hh"
#include "G4Region.hh"

#include "G4Box.hh"
// #include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4RotationMatrix.hh"
#include "G4SubtractionSolid.hh"
#include "G4UnionSolid.hh"
#include "G4NistManager.hh"
#include "G4ThreeVector.hh"

// #include "G4VSolid.hh"
#include "G4Orb.hh"
#include "G4Tubs.hh"
#include "G4VisAttributes.hh"
#include "G4Colour.hh"
#include "G4Exception.hh"
#include <chrono>


//--------------------------------------------------------------------------------------------------
// Structure containting coordinates of volumes in a DNA base pair.
// Identical to the definition in GeoCalculation.
//--------------------------------------------------------------------------------------------------
struct DNAPlacementData
{
    G4ThreeVector posCenterDNA;
    G4ThreeVector posSugarTMP1;
    G4ThreeVector posSugarTHF1;
    G4ThreeVector posBase1;
    G4ThreeVector posBase2;
    G4ThreeVector posSugarTHF2;
    G4ThreeVector posSugarTMP2;
};

TsFiberV2::TsFiberV2(TsParameterManager* pM, TsExtensionManager* eM, TsMaterialManager* mM, TsGeometryManager* gM,
			 TsVGeometryComponent* parentComponent, G4VPhysicalVolume* parentVolume, G4String& name) :
TsVGeometryComponent(pM, eM, mM, gM, parentComponent, parentVolume, name)
{  
    fGeoManager = new GeoManagerV2(0, 1.);   
    fGeoCalculation = new GeoCalculationV2(0, 1.);

    fpDnaMoleculePositions = new std::map<G4String, std::vector<std::vector<G4double> > >();
}


TsFiberV2::~TsFiberV2()
{
     delete fGeoManager;
     delete fGeoCalculation;

     delete fpDnaMoleculePositions;
}


G4VPhysicalVolume* TsFiberV2::Construct()
{
	BeginConstruction();

    auto start_total = std::chrono::high_resolution_clock::now();

    G4double* envelopeDimensions = fPm->GetDoubleVector(GetFullParmName("Dimensions"),"Length");
    
    //Wrapping component for whole fiber    
    G4Tubs* sWrapper = new G4Tubs("solid_wrapper", 0., envelopeDimensions[0], envelopeDimensions[1], 0, 360);
    fEnvelopeLog = CreateLogicalVolume(sWrapper);
    fEnvelopePhys = CreatePhysicalVolume(fEnvelopeLog);

    // Get some parameters
    G4int numBpPerNucleosome = fPm->GetIntegerParameter(GetFullParmName("DNANumBpPerNucleosome"));
    G4int numNucleosomePerFiber = fPm->GetIntegerParameter(GetFullParmName("DnaNumNucleosomePerFiber"));
    fCutVolumes = fPm->GetBooleanParameter(GetFullParmName("CutVolumes"));
    fCheckForOverlaps = fPm->GetBooleanParameter("Ge/CheckForOverlaps");
    fOverlapsResolution = fPm->GetIntegerParameter("Ge/CheckForOverlapsResolution");
    fQuitIfOverlap = fPm->GetBooleanParameter("Ge/QuitIfOverlapDetected");
    fUseG4Volumes = fPm->GetBooleanParameter("Ge/MyDNA/UseG4Volumes");

    // ---------------------------------------------------------------------------------------------
    // New stuff
    // ---------------------------------------------------------------------------------------------
    fVerbose = 0;
    fFactor = 1.;

    fGeoCalculation->Initialize();

    fBaseRadius = fGeoCalculation->GetBaseRadius();
    fBaseRadiusWater = fGeoCalculation->GetBaseRadiusWater();
    fSugarTMPRadius = fGeoCalculation->GetSugarTMPRadius();
    fSugarTHFRadius = fGeoCalculation->GetSugarTHFRadius();
    fSugarTMPRadiusWater = fGeoCalculation->GetSugarTMPRadiusWater();
    fSugarTHFRadiusWater = fGeoCalculation->GetSugarTHFRadiusWater();

    fHistoneHeight = fGeoCalculation->GetHistoneHeight() ;
    fHistoneRadius = fGeoCalculation->GetHistoneRadius();
    fNucleoNum = numNucleosomePerFiber;
    fBpNum = numBpPerNucleosome;
    fFiberPitch = fGeoCalculation->GetFiberPitch();
    fFiberDeltaAngle = fGeoCalculation->GetFiberDeltaAngle() ;
    fFiberNbNuclPerTurn = fGeoCalculation->GetFiberNbNuclPerTurn();

    // fWaterName = fParentComponent->GetResolvedMaterialName();
    // fWater = G4NistManager::Instance()->FindOrBuildMaterial(fWaterName);
    fWaterName = "G4_WATER";
    fWater = GetMaterial(fWaterName);

    // Create modified water material to be used in DNA volumes (used to identify volumes in which
    // to score damage)
    fDNAMaterialName = "G4_WATER_CLONE";
    fDNAMaterial = GetMaterial(fDNAMaterialName);
    // fDNAMaterial = G4NistManager::Instance()->BuildMaterialWithNewDensity(fDNAMaterialName,"G4_WATER",1.000*g/cm3);
    // G4String fDNAMaterialName = fParentComponent->GetResolvedMaterialName();
    // if ( fPm->ParameterExists(GetFullParmName("DNAMaterialName")))
    //     fDNAMaterialName = fPm->GetStringParameter(GetFullParmName("DNAMaterialName"));
    // fDNAMaterial = G4NistManager::Instance()->FindOrBuildMaterial("G4_WATER");
    // ---------------------------------------------------------------------------------------------


    // Build DNA in the fiber
    // fGeoManager->Initialize(numBpPerNucleosome,numNucleosomePerFiber);
    // G4LogicalVolume* lFiber = fGeoManager->BuildLogicFiber(fDNAMaterial, fCutVolumes, fCheckForOverlaps, 
    //     fOverlapsResolution, fQuitIfOverlap);
    G4LogicalVolume* lFiber = BuildLogicFiber(fGeoCalculation->GetAllDNAVolumePositions(),
                     fGeoCalculation->GetNucleosomePosition(),
                     fGeoCalculation->GetPosAndRadiusMap(),
                     fCutVolumes,
                     fCheckForOverlaps,
                     fOverlapsResolution,
                     fQuitIfOverlap);

    // pFiber = CreatePhysicalVolume("Fiber", lFiber, fEnvelopePhys);
    // G4VPhysicalVolume* pFiber = CreatePhysicalVolume("Fiber", lFiber, fEnvelopePhys);
    // G4ThreeVector emptyPlacement = G4ThreeVector(0.,0.,0.);
    // G4VPhysicalVolume* pFiber = new G4PVPlacement(0,emptyPlacement,lFiber,"Fiber",fEnvelopeLog,false,0);
    // G4VPhysicalVolume* pFiber;
    if (fUseG4Volumes){
        G4ThreeVector emptyPlacement = G4ThreeVector(0.,0.,0.);
        pFiber = new G4PVPlacement(0,emptyPlacement,lFiber,"Fiber",fEnvelopeLog,false,0);
    }
    // else{
    //     pFiber = CreatePhysicalVolume("Fiber", lFiber, fEnvelopePhys);
    // }


    // Check overlaps. Although topas does this check automatically, it does so before building
    // logic fiber. Need to redo afterwards (i.e. after adding DNA content).
    // if (fCheckForOverlaps) {
    //     if(pFiber->CheckOverlaps(fOverlapsResolution) && fQuitIfOverlap)
    //         ThrowOverlapError();
    // }

    auto finish_total = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_total = finish_total-start_total;
    G4cout << "Total time to generate geometry = " << elapsed_total.count() << " s" << G4endl;  
    
	return fEnvelopePhys;
}

G4Material * TsFiberV2::OtherMaterial(G4String materialName)
{
    G4Material * material(0);
    
    if(materialName == "G4_WATER"){
        // Water is defined from NIST material database
        G4NistManager * man = G4NistManager::Instance();
        material = man->FindOrBuildMaterial("G4_WATER");

    }
    
    return material;
}

//--------------------------------------------------------------------------------------------------
// Helper function used if a geometrical overlap is detected. Return the standard Topas message
// about geometry overlaps and exit gracefully.
//--------------------------------------------------------------------------------------------------
void TsFiberV2::ThrowOverlapError() 
{
    G4cout << "Topas is quitting due to the above geometry overlap problem." << G4endl;
    G4cout << "If you still want the TOPAS session to continue" << G4endl;
    G4cout << "(such as to use visualization to study the overlap)," << G4endl;
    G4cout << "Set the parameter Ge/QuitIfOverlapDetected to False" << G4endl;
    exit(0);
}

//--------------------------------------------------------------------------------------------------
// Create and return a logical volume for a chromatin fiber. 
// Within this logical volume are the physical volumes for the histones, the resiudes and their
// hydration shells. Solids and logicals are generated for the histones within this metohd directly,
// whereas those for the residues are generated using CreateNucleosomeCuttedSolidsAndLogicals().
// A map, fpDnaMoleculePositions, containing the coordinates for all residues and histones is filled
// and can be accessed using GetDNAMoleculesPositions().
//--------------------------------------------------------------------------------------------------
G4LogicalVolume* TsFiberV2::BuildLogicFiber(std::vector<std::vector<DNAPlacementData> >* dnaVolPos,
                                            std::vector<G4ThreeVector>* posNucleo,
                                            std::map<G4ThreeVector, G4double>* posAndRadiusMap,
                                            G4bool cutVolumes,
                                            G4bool checkForOverlaps,
                                            G4int overlapsResolution,
                                            G4bool quitIfOverlap)
{
    //----------------------------------------------------------------------------------------------
    // Throw error if any of the member variables haven't been initialized correctly.
    //----------------------------------------------------------------------------------------------
    if(fSugarTHFRadius==-1||fSugarTMPRadius==-1||fBaseRadius==-1||fFiberPitch==-1||fFiberNbNuclPerTurn==-1
            ||fNucleoNum==-1||fBpNum==-1||fHistoneHeight==-1||fHistoneRadius==-1)
    {
        G4cerr<<"FatalError: GeoVolumeV2::BuildLogicFiber. A class parameter has not been " 
            << "initialized and its value is still negative"<<G4endl;
        std::exit(EXIT_FAILURE);
    }
    //----------------------------------------------------------------------------------------------
    // Create cylindrical fiber volume
    //----------------------------------------------------------------------------------------------
    G4Tubs* solidFiber = new G4Tubs("solid histone", 0., 17.*fFactor*nm, 68.*fFactor*nm, 0, 360); // radius & half length

    G4VisAttributes fiberVis(G4Colour(1.0, 1.0, 1.0, 0.1) );
    // fiberVis.SetVisibility(false);
    fiberVis.SetForceSolid(true);
    // fiberVis.SetForceWireframe(true);

    // G4LogicalVolume* logicFiber = new G4LogicalVolume(solidFiber, fWater,"logic_fiber");
    G4LogicalVolume* logicFiber;
    if (fUseG4Volumes) {
        logicFiber = new G4LogicalVolume(solidFiber,fWater,"logic_fiber");
    }
    else {
        logicFiber = CreateLogicalVolume("logic_fiber",fWaterName,solidFiber);
    }  
    logicFiber->SetVisAttributes(fiberVis);

    if (!fUseG4Volumes){
        pFiber = CreatePhysicalVolume("Fiber", logicFiber, fEnvelopePhys);
    }

        // G4VPhysicalVolume* pFiber;
        // if (fUseG4Volumes){
        // G4ThreeVector emptyPlacement = G4ThreeVector(0.,0.,0.);
        // pFiber = new G4PVPlacement(0,emptyPlacement,logicFiber,"Fiber",fEnvelopeLog,false,0);
        // }
        // else{
        // pFiber = CreatePhysicalVolume("Fiber", logicFiber, fEnvelopePhys);
        // }
    // }

    //----------------------------------------------------------------------------------------------
    // Create the histone volume
    //----------------------------------------------------------------------------------------------
    G4Tubs* solidHistone = new G4Tubs("solid histone", 0., fHistoneRadius, fHistoneHeight, 0, 360);

    G4VisAttributes histoneVis(G4Colour(1.0, 1.0, 1.0) );
    // histoneVis.SetVisibility(false);
    histoneVis.SetForceSolid(true);
    // G4LogicalVolume* logicHistone = new G4LogicalVolume(solidHistone,fWater,"logic_histone");
    G4LogicalVolume* logicHistone;
    if (fUseG4Volumes) {
        logicHistone = new G4LogicalVolume(solidHistone,fWater,"logic_histone");
    }
    else {
        logicHistone = CreateLogicalVolume("logic_histone",fWaterName,solidHistone);
    }  
    logicHistone->SetVisAttributes(histoneVis);

    //----------------------------------------------------------------------------------------------
    // Once the material for the fibre & nucleosomes has been defined (above), change the material
    // for the DNA elements (residues).
    //----------------------------------------------------------------------------------------------
    // fWater = DNAMaterial;

    //----------------------------------------------------------------------------------------------
    // Generate the cut solids
    //----------------------------------------------------------------------------------------------
    // For the positions, we only use the second (number 1) nucleosome.
    // Indeed, it is a "middle" nucleosome and, thus, the two extemities will be cutted.
    // If we took the first, we would have a first volume non cutted and, thus, some overlaps.
    // Note dnaVolPos is paramater passed to BuildLogicFiber() that is essentially the output of 
    // GeoCalculation::CalculateDNAPosition(). I.e. all nucleotide positions around 3 basis
    // histone complexes.
    std::vector<DNAPlacementData>* nuclVolPos = &dnaVolPos->at(1);

    // We create here all the DNA volumes (solid & logical) around the histone based on the second 
    // nucleosome (number 1) positions. Only the volumes of the second nucleosome are generated. By 
    // placing them several times we will build the fiber. This is done to save memory and improve
    // speed. We saved the volumes in a map (key = name of the volume [e.g. sugar1], value = vector
    // of corresponding logical volumes).
    // Note posAndRadiusMap is parameter passed to BuildLogicFiber() that is essentially the output
    // of GeoCalculation::GenerateCoordAndRadiusMap(). I.e. a map of water radii for 6 volumes in
    // each of 200 bp in each of 3 basis nucleosomes (3600 volumes)
    std::map<G4String, std::vector<G4LogicalVolume*> >* volMap
            = CreateNucleosomeCuttedSolidsAndLogicals(nuclVolPos, posAndRadiusMap, cutVolumes);
    // Resulting volMap is indexed by one of 12 entries (6 residues & 6 hydration shells). Each
    // entry has 200 elements, each corresponding to a distinct logical volume

    // Print the mean volume number of the DNA volumes
    if(fVerbose>2)
    {
        CalculateMeanVol(volMap);
    }

    //----------------------------------------------------------------------------------------------
    // Save the positions of all residue volumes in the first nucleosome (not all 3) in vectors
    //----------------------------------------------------------------------------------------------
    G4ThreeVector posSugarTMP1;
    G4ThreeVector posSugarTHF1;
    G4ThreeVector posBase1;
    G4ThreeVector posBase2;
    G4ThreeVector posSugarTHF2;
    G4ThreeVector posSugarTMP2;
    std::vector<G4ThreeVector> posSugarTMP1Vect;
    std::vector<G4ThreeVector> posSugarTHF1Vect;
    std::vector<G4ThreeVector> posBase1Vect;
    std::vector<G4ThreeVector> posBase2Vect;
    std::vector<G4ThreeVector> posSugarTHF2Vect;
    std::vector<G4ThreeVector> posSugarTMP2Vect;

    // Note: fBpNum is input in parameter file. Default = 200
    // Get the bp volume positions of the middle basis nucleosome (which is used to generate cut 
    // solids) and save in a vector to be accessed & rotated in the following forloop
    for(int j=0;j<fBpNum;++j)
    {
        
        posSugarTMP1 = dnaVolPos->at(1)[j].posSugarTMP1;
        posSugarTHF1 = dnaVolPos->at(1)[j].posSugarTHF1;
        posBase1 = dnaVolPos->at(1)[j].posBase1;
        posBase2 = dnaVolPos->at(1)[j].posBase2;
        posSugarTHF2 = dnaVolPos->at(1)[j].posSugarTHF2;
        posSugarTMP2 = dnaVolPos->at(1)[j].posSugarTMP2;

        posSugarTMP1Vect.push_back(posSugarTMP1);
        posSugarTHF1Vect.push_back(posSugarTHF1);
        posBase1Vect.push_back(posBase1);
        posBase2Vect.push_back(posBase2);
        posSugarTHF2Vect.push_back(posSugarTHF2);
        posSugarTMP2Vect.push_back(posSugarTMP2);
    }

    //----------------------------------------------------------------------------------------------
    // Save the first histone position
    //----------------------------------------------------------------------------------------------
    G4ThreeVector posHistone = posNucleo->at(0);

    //----------------------------------------------------------------------------------------------
    // Do the placements; i.e. build the nucleosome helix inside the fiber using G4PVPlacements of
    // the already-determined logical volumes
    //----------------------------------------------------------------------------------------------
    // Distance value used to place the volumes at the beginning of the fiber
    G4ThreeVector minusForFiber = G4ThreeVector(0.,0.,-solidFiber->GetDz() + fHistoneHeight);

    G4double zShift = fFiberPitch/fFiberNbNuclPerTurn; // z shift for each new nuclosome
    G4int count = 0;

    //----------------------------------------------------------------------------------------------
    // iterate on each nucleosome (fNucleoNum is input in parameter file. Default = 90).
    //----------------------------------------------------------------------------------------------
    for(int i=0;i<fNucleoNum;++i)
    {
        auto start = std::chrono::high_resolution_clock::now();
        // Rotates the nucleosome itself with respect to the z-axis, in order to align the cut
        // volumes appropriately to prevent overlaps. This will be appliedThis is not the same as placing a nucleosome
        // at the next position around the fiber. That is done below. Our basis nucleosome is
        // actually the second one (middle). The following rotation logic accounts for this.

        // This rotation object will be applied to every physical volume placement below, in order
        // to align the cut bp volumes and prevent overlaps. This is a rotation about the
        // volume's own z-axis.
        G4RotationMatrix* rotCuts = new G4RotationMatrix();
        rotCuts->rotateZ((i-1)*-fFiberDeltaAngle);

        //------------------------------------------------------------------------------------------
        // iterate on each bp (fBpNum is input in parameter file. Default = 200)
        //------------------------------------------------------------------------------------------
        for(int j=0;j<fBpNum;++j)
        {
            //--------------------------------------------------------------------------------------
            // The following positional rotations account for the fact that nucleosomes are
            // assembled in a helical fashion around the fibre axis (z). Note this has nothing to
            // do with rotations of the bp volumes themselves (which are handled by rotCuts obj).
            //--------------------------------------------------------------------------------------
            // Since our basis nucleosome is the middle one, when i==1, there should be no rotation.
            // Before and after this, there are corresponding rotations about the z axis.
            if(i==0)
            {
                posSugarTMP1 = posSugarTMP1Vect[j].rotateZ(-fFiberDeltaAngle);
                posSugarTHF1 = posSugarTHF1Vect[j].rotateZ(-fFiberDeltaAngle);
                posBase1 = posBase1Vect[j].rotateZ(-fFiberDeltaAngle);
                posBase2 = posBase2Vect[j].rotateZ(-fFiberDeltaAngle);
                posSugarTHF2 = posSugarTHF2Vect[j].rotateZ(-fFiberDeltaAngle);
                posSugarTMP2 = posSugarTMP2Vect[j].rotateZ(-fFiberDeltaAngle);  
            }
            else
            {
                posSugarTMP1 = posSugarTMP1Vect[j].rotateZ(fFiberDeltaAngle);
                posSugarTHF1 = posSugarTHF1Vect[j].rotateZ(fFiberDeltaAngle);
                posBase1 = posBase1Vect[j].rotateZ(fFiberDeltaAngle);
                posBase2 = posBase2Vect[j].rotateZ(fFiberDeltaAngle);
                posSugarTHF2 = posSugarTHF2Vect[j].rotateZ(fFiberDeltaAngle);
                posSugarTMP2 = posSugarTMP2Vect[j].rotateZ(fFiberDeltaAngle);   
            }

            //--------------------------------------------------------------------------------------
            // Apply a z shift, specific to each nucleosome, to span length of fiber
            //--------------------------------------------------------------------------------------
            posSugarTMP1 += G4ThreeVector(0.,0.,i*zShift-zShift);
            posSugarTHF1 += G4ThreeVector(0.,0.,i*zShift-zShift);
            posBase1 += G4ThreeVector(0.,0.,i*zShift-zShift);
            posBase2 += G4ThreeVector(0.,0.,i*zShift-zShift);
            posSugarTHF2 += G4ThreeVector(0.,0.,i*zShift-zShift);
            posSugarTMP2 += G4ThreeVector(0.,0.,i*zShift-zShift);

            //--------------------------------------------------------------------------------------
            // Apply shift such that fiber helix construction begins at at one end.
            //--------------------------------------------------------------------------------------
            posSugarTMP1 += minusForFiber;
            posSugarTHF1 += minusForFiber;
            posBase1 += minusForFiber;
            posBase2 += minusForFiber;
            posSugarTHF2 += minusForFiber;
            posSugarTMP2 += minusForFiber;

            //--------------------------------------------------------------------------------------
            // Place physical volumes. Residue volumes are placed inside of corresponding water
            // volumes (i.e. water volumes are mother volumes). 5 values for each physical volume
            // are then recorded as a vector, specific to the current bp index, within the correct
            // map key:value pair of fpDnaMoleculePositions. Note the physical volumes themselves
            // not recorded in the map.  
            // These values are: x, y, z, bp index (i.e. 1 - 200), nucleotide index (i.e. 1 or 2).
            // Subsequently (below), the water volumes are placed inside the cylindrical logicFiber 
            // volume. 
            // e.g. To get the x coordinate of the 150th sugar volume in the second DNA strand of 
            // the 7th nucleosome: (*fpDnaMoleculePositions)["Desoxyribose"][(6*200*2)+(149*2)+2][0]
            //--------------------------------------------------------------------------------------
            G4int bp_index = (i*fBpNum)+j;
            G4String bp_index_string = std::to_string(bp_index);
            // Phosphate 1
            G4String phys_name = "p_1_" + bp_index_string;
            // G4PVPlacement* sTMP1 = new G4PVPlacement(rotCuts,posSugarTMP1,volMap->at("sugarTMP1")[j],
            //     phys_name,logicFiber,false,count);
            // G4VPhysicalVolume* sTMP1 = CreatePhysicalVolume(phys_name,count,false,volMap->at("sugarTMP1")[j],rotCuts,&posSugarTMP1,pFiber);
            G4VPhysicalVolume* sTMP1;
            if (fUseG4Volumes){
                sTMP1 = new G4PVPlacement(rotCuts,posSugarTMP1,volMap->at("sugarTMP1")[j],
                    phys_name,logicFiber,false,count);
            }
            else{
                sTMP1 = CreatePhysicalVolume(phys_name,count,true,volMap->at("sugarTMP1")[j],rotCuts,&posSugarTMP1,pFiber);
            }
            (*fpDnaMoleculePositions)["Phosphate"].push_back(std::vector<double>());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(posSugarTMP1.getX());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(posSugarTMP1.getY());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(posSugarTMP1.getZ());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(count);
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(1);
            // Sugar 1
            phys_name = "s_1_" + bp_index_string;
            // G4PVPlacement* sTHF1 = new G4PVPlacement(rotCuts,posSugarTHF1,volMap->at("sugarTHF1")[j],
            //     phys_name,logicFiber,false,count+100000);
            // G4VPhysicalVolume* sTHF1 = CreatePhysicalVolume(phys_name,count+100000,false,volMap->at("sugarTHF1")[j],rotCuts,&posSugarTHF1,pFiber);
            G4VPhysicalVolume* sTHF1;
            if (fUseG4Volumes){
                sTHF1 = new G4PVPlacement(rotCuts,posSugarTHF1,volMap->at("sugarTHF1")[j],
                    phys_name,logicFiber,false,count+100000);
            }
            else{
                sTHF1 = CreatePhysicalVolume(phys_name,count+100000,true,volMap->at("sugarTHF1")[j],rotCuts,&posSugarTHF1,pFiber);
            }
            (*fpDnaMoleculePositions)["Desoxyribose"].push_back(std::vector<double>());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(posSugarTHF1.getX());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(posSugarTHF1.getY());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(posSugarTHF1.getZ());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(count);
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(1);
            // Base 1
            phys_name = "b_1_" + bp_index_string;
            // G4PVPlacement* base1 = new G4PVPlacement(rotCuts,posBase1,volMap->at("base1")[j],phys_name,
            //         logicFiber,false,count+200000);
            // G4VPhysicalVolume* base1 = CreatePhysicalVolume(phys_name,count+200000,false,volMap->at("base1")[j],rotCuts,&posBase1,pFiber);
            G4VPhysicalVolume* base1;
            if (fUseG4Volumes){
                base1 = new G4PVPlacement(rotCuts,posBase1,volMap->at("base1")[j],phys_name,
                        logicFiber,false,count+200000);
            }
            else{
                base1 = CreatePhysicalVolume(phys_name,count+200000,true,volMap->at("base1")[j],rotCuts,&posBase1,pFiber);
            }
            (*fpDnaMoleculePositions)["Base1"].push_back(std::vector<double>());
            (*fpDnaMoleculePositions)["Base1"].back().push_back(posBase1.getX());
            (*fpDnaMoleculePositions)["Base1"].back().push_back(posBase1.getY());
            (*fpDnaMoleculePositions)["Base1"].back().push_back(posBase1.getZ());
            (*fpDnaMoleculePositions)["Base1"].back().push_back(count);
            (*fpDnaMoleculePositions)["Base1"].back().push_back(1);
            // Base 2
            phys_name = "b_2_" + bp_index_string;
            // G4PVPlacement* base2 = new G4PVPlacement(rotCuts,posBase2,volMap->at("base2")[j],phys_name,
            //                      logicFiber,false,count+1200000);
            // G4VPhysicalVolume* base2 = CreatePhysicalVolume(phys_name,count+1200000,false,volMap->at("base2")[j],rotCuts,&posBase2,pFiber);
            G4VPhysicalVolume* base2;
            if (fUseG4Volumes){
                base2 = new G4PVPlacement(rotCuts,posBase2,volMap->at("base2")[j],phys_name,
                                     logicFiber,false,count+1200000);
            }
            else{
                base2 = CreatePhysicalVolume(phys_name,count+1200000,true,volMap->at("base2")[j],rotCuts,&posBase2,pFiber);
            }
            (*fpDnaMoleculePositions)["Base2"].push_back(std::vector<double>());
            (*fpDnaMoleculePositions)["Base2"].back().push_back(posBase2.getX());
            (*fpDnaMoleculePositions)["Base2"].back().push_back(posBase2.getY());
            (*fpDnaMoleculePositions)["Base2"].back().push_back(posBase2.getZ());
            (*fpDnaMoleculePositions)["Base2"].back().push_back(count);
            (*fpDnaMoleculePositions)["Base2"].back().push_back(2);
            // Sugar 2
            phys_name = "s_2_" + bp_index_string;
            // G4PVPlacement* sTHF2 = new G4PVPlacement(rotCuts,posSugarTHF2,volMap->at("sugarTHF2")[j],
            //     phys_name,logicFiber,false,count+1100000);
            // G4VPhysicalVolume* sTHF2 = CreatePhysicalVolume(phys_name,count+1100000,false,volMap->at("sugarTHF2")[j],rotCuts,&posSugarTHF2,pFiber);
            G4VPhysicalVolume* sTHF2;
            if (fUseG4Volumes){
                sTHF2 = new G4PVPlacement(rotCuts,posSugarTHF2,volMap->at("sugarTHF2")[j],
                    phys_name,logicFiber,false,count+1100000);
            }
            else{
                sTHF2 = CreatePhysicalVolume(phys_name,count+1100000,true,volMap->at("sugarTHF2")[j],rotCuts,&posSugarTHF2,pFiber);
            }
            (*fpDnaMoleculePositions)["Desoxyribose"].push_back(std::vector<double>());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(posSugarTHF2.getX());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(posSugarTHF2.getY());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(posSugarTHF2.getZ());
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(count);
            (*fpDnaMoleculePositions)["Desoxyribose"].back().push_back(2);
            // Phosphate 2
            phys_name = "p_2_" + bp_index_string;
            // G4PVPlacement* sTMP2 = new G4PVPlacement(rotCuts,posSugarTMP2,volMap->at("sugarTMP2")[j],
            //     phys_name,logicFiber,false,count+1000000);
            // G4VPhysicalVolume* sTMP2 = CreatePhysicalVolume(phys_name,count+1000000,false,volMap->at("sugarTMP2")[j],rotCuts,&posSugarTMP2,pFiber);
            G4VPhysicalVolume* sTMP2;
            if (fUseG4Volumes){
                sTMP2 = new G4PVPlacement(rotCuts,posSugarTMP2,volMap->at("sugarTMP2")[j],
                    phys_name,logicFiber,false,count+1000000);
            }
            else{
                sTMP2 = CreatePhysicalVolume(phys_name,count+1000000,true,volMap->at("sugarTMP2")[j],rotCuts,&posSugarTMP2,pFiber);
            }
            (*fpDnaMoleculePositions)["Phosphate"].push_back(std::vector<double>());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(posSugarTMP2.getX());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(posSugarTMP2.getY());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(posSugarTMP2.getZ());
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(count);
            (*fpDnaMoleculePositions)["Phosphate"].back().push_back(2);
            // Place water volumes (containing residue placements) inside fiber volume
            // G4PVPlacement* sTMP1W = new G4PVPlacement(0,posSugarTMP1,
            //     volMap->at("sugarTMP1Water")[j],"sugarTMP1Hydra",logicFiber,false,count);
            // G4PVPlacement* sTHF1W = new G4PVPlacement(0,posSugarTHF1,
            //     volMap->at("sugarTHF1Water")[j],"sugarTHF1Hydra",logicFiber,false,count);
            // G4PVPlacement* b1W = new G4PVPlacement(0,posBase1,
            //     volMap->at("base1Water")[j],"base1Hydra",logicFiber,false,count);
            // G4PVPlacement* b2W = new G4PVPlacement(0,posBase2,
            //     volMap->at("base2Water")[j],"base2Hydra",logicFiber,false,count);
            // G4PVPlacement* sTHF2W = new G4PVPlacement(0,posSugarTHF2,
            //     volMap->at("sugarTHF2Water")[j],"sugarTHF2Hydra",logicFiber,false,count);
            // G4PVPlacement* sTMP2W = new G4PVPlacement(0,posSugarTMP2,
            //     volMap->at("sugarTMP2Water")[j],"sugarTMP2Hydra",logicFiber,false,count);

            // Check overlaps if solids have been cut. No need if uncut, b/c will overlap
            // Throw and overlap error if settings request as such
            if (checkForOverlaps) {
                G4bool overlapDetected = false;
                if(sTMP1->CheckOverlaps(overlapsResolution) && quitIfOverlap)
                    ThrowOverlapError();
                if(sTHF1->CheckOverlaps(overlapsResolution) && quitIfOverlap)
                    ThrowOverlapError();
                if(base1->CheckOverlaps(overlapsResolution) && quitIfOverlap)
                    ThrowOverlapError();
                if(base2->CheckOverlaps(overlapsResolution) && quitIfOverlap)
                    ThrowOverlapError();
                if(sTHF2->CheckOverlaps(overlapsResolution) && quitIfOverlap)
                    ThrowOverlapError();
                if(sTMP2->CheckOverlaps(overlapsResolution) && quitIfOverlap)
                    ThrowOverlapError();
                // sTMP1W->CheckOverlaps();
                // sTHF1W->CheckOverlaps();
                // b1W->CheckOverlaps();
                // b2W->CheckOverlaps();
                // sTHF2W->CheckOverlaps();
                // sTMP2W->CheckOverlaps();


                // 
                // Simulation results can not be trusted when any overlap exists.
                // If you still want the TOPAS session to continue
                // (such as to use visualization to study the overlap),
                // Set the parameter Ge/QuitIfOverlapDetected to False
            }

            ++count;
        }

        //------------------------------------------------------------------------------------------
        // Place the histone volume
        //------------------------------------------------------------------------------------------
        // Rotate. Not really necessary
        G4ThreeVector posHistoneForNucleo = posHistone;
        posHistoneForNucleo.rotateZ(i*fFiberDeltaAngle);
        // Apply a z shift, specific to each nucleosome, to span length of fiber
        posHistoneForNucleo += G4ThreeVector(0.,0.,i*zShift);
        // Apply shift such that fiber helix construction begins at at one end.
        posHistoneForNucleo += minusForFiber;
        // Place
        G4String histName = "histone_" + std::to_string(i);
        // G4VPhysicalVolume* pHistone = new G4PVPlacement(0,posHistoneForNucleo,logicHistone,histName,logicFiber,true,i);
        // G4VPhysicalVolume* pHistone = CreatePhysicalVolume(histName,i,true,logicHistone,0,&posHistoneForNucleo,pFiber);
        G4VPhysicalVolume* pHistone;
        if (fUseG4Volumes){
            pHistone = new G4PVPlacement(0,posHistoneForNucleo,logicHistone,histName,logicFiber,true,i);
        }
        else{
            pHistone = CreatePhysicalVolume(histName,i,true,logicHistone,0,&posHistoneForNucleo,pFiber);
        }
        (*fpDnaMoleculePositions)["Histone"].push_back(std::vector<double>());
        (*fpDnaMoleculePositions)["Histone"].back().push_back(posHistoneForNucleo.getX());
        (*fpDnaMoleculePositions)["Histone"].back().push_back(posHistoneForNucleo.getY());
        (*fpDnaMoleculePositions)["Histone"].back().push_back(posHistoneForNucleo.getZ());
        (*fpDnaMoleculePositions)["Histone"].back().push_back(i);
        (*fpDnaMoleculePositions)["Histone"].back().push_back(0);

        if (checkForOverlaps) {
            if(pHistone->CheckOverlaps(overlapsResolution) && quitIfOverlap)
                ThrowOverlapError();
        }
        auto finish = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = finish-start;
        G4cout << "Time to nucleosome #" << i << " = " << elapsed.count() << " s" << G4endl;  
    }

    // logicFiber contains the placements of the histones and the water volumes, which in turn
    // contain the placements of the residues. 
    // Note: The map of positions (fpDnaMoleculePositions) is not returned explicitly but is
    // accessible via a getter method, GetDNAMoleculesPositions() GeoManager has a wrapper for this,
    // also called GetDNAMoleculesPositions()
    // CalculateMeanVol(volMap);
    return logicFiber;
}


//--------------------------------------------------------------------------------------------------
// Create the solid and logical volumes required to build DNA around one histone.
// Return a map as:
// Key: name of the volume (base1, base2, base1Water, ...). Size = 12.
// Content: vector of corresponding logical volumes (each vector size = 200)
//--------------------------------------------------------------------------------------------------
std::map<G4String, std::vector<G4LogicalVolume*> >* TsFiberV2::CreateNucleosomeCuttedSolidsAndLogicals(
    std::vector<DNAPlacementData>* nucleosomeVolumePositions, std::map<G4ThreeVector, 
    G4double>* posAndRadiusMap, G4bool cutVolumes)
{
    // This is the map to be returned
    std::map<G4String, std::vector<G4LogicalVolume*> >* logicSolidsMap = new std::map<G4String, std::vector<G4LogicalVolume*> >;

    G4int basePairNum = nucleosomeVolumePositions->size(); // 200

    //----------------------------------------------------------------------------------------------
    // Create elementary solids
    //----------------------------------------------------------------------------------------------
    // Throw error if a member variables hasn't been initialized correctly.
    if(fSugarTHFRadius==-1 || fSugarTMPRadius==-1 || fBaseRadius==-1)
    {
        G4cerr<<"************************************************************"<<G4endl;
        G4cerr<<"GeoVolumeV2::CreateNucleosomeCuttedSolidsAndLogicals: fSugarTHFRadius, "
            << "fSugarTMPRadius or fBaseRadius were not set. Fatal error."<<G4endl;
        G4cerr<<"************************************************************"<<G4endl;
        std::exit(EXIT_FAILURE);
    }

    //----------------------------------------------------------------------------------------------
    // Visibility attributes for nucleotide components (base, sugar, phosphate, shell)
    //----------------------------------------------------------------------------------------------
    // G4VisAttributes red(G4Colour(1.0, 0.0, 0.0) );
    // G4VisAttributes blue(G4Colour(0.0, 0.0, 1.0, 0.0) );
    // G4VisAttributes green(G4Colour(0.0, 1.0, 0.0) );
    // G4VisAttributes yellow(G4Colour(1.0, 1.0, 0.0) );
    // red.SetForceSolid(true);
    // // red.SetVisibility(false);
    // green.SetForceSolid(true);
    // yellow.SetForceSolid(true);

    G4VisAttributes visBase(G4Colour(0.92, 0.6, 0.6));    
    G4VisAttributes visSugar(G4Colour(0.43, 0.62, 0.92));    
    G4VisAttributes visPhosphate(G4Colour(0.71, 0.65, 0.84)); 
    G4VisAttributes visHydration(G4Colour(0.27, 0.82, 0.82)); 
    visBase.SetForceSolid(true);
    visSugar.SetForceSolid(true);
    visPhosphate.SetForceSolid(true);
    // visBase.SetForceWireframe(true);
    // visSugar.SetForceWireframe(true);
    // visPhosphate.SetForceWireframe(true);
    // visBase.SetVisibility(false);
    // visSugar.SetVisibility(false);
    // visPhosphate.SetVisibility(false);

    // visHydration.SetForceSolid(true);
    visHydration.SetForceWireframe(true);
    // visHydration.SetVisibility(true);
    visHydration.SetVisibility(false);

    //----------------------------------------------------------------------------------------------
    // Create solid volumes
    //----------------------------------------------------------------------------------------------
    // residues
    G4Orb* solidSugarTHF = new G4Orb("solid_sugar_THF", fSugarTHFRadius);
    G4Orb* solidSugarTMP = new G4Orb("solid_sugar_TMP", fSugarTMPRadius);
    G4Orb* solidBase = new G4Orb("solid_base", fBaseRadius);

    // hydration shells
    G4Orb* solidSugarTHFWater = new G4Orb("solid_sugar_THF_Water", fSugarTHFRadiusWater);
    G4Orb* solidSugarTMPWater = new G4Orb("solid_sugar_TMP_Water", fSugarTMPRadiusWater);
    G4Orb* solidBaseWater = new G4Orb("solid_base_Water", fBaseRadiusWater);

    //----------------------------------------------------------------------------------------------
    // Position variables of resiudes
    //---------------------------------------------------------------------------------------------- 
    G4ThreeVector posSugarTMP1;
    G4ThreeVector posSugarTHF1;
    G4ThreeVector posBase1;
    G4ThreeVector posBase2;
    G4ThreeVector posSugarTHF2;
    G4ThreeVector posSugarTMP2;

    //----------------------------------------------------------------------------------------------
    // Iterate on each base pair to generate cut solids and logical volumes.
    //----------------------------------------------------------------------------------------------
    for(int j=0;j<fBpNum;++j)
    {
        //------------------------------------------------------------------------------------------
        // First: cut the solids (if no visualization)
        //------------------------------------------------------------------------------------------
        // Get the position
        posSugarTMP1 = nucleosomeVolumePositions->at(j).posSugarTMP1;
        posSugarTHF1 = nucleosomeVolumePositions->at(j).posSugarTHF1;
        posBase1 = nucleosomeVolumePositions->at(j).posBase1;
        posBase2 = nucleosomeVolumePositions->at(j).posBase2;
        posSugarTHF2 = nucleosomeVolumePositions->at(j).posSugarTHF2;
        posSugarTMP2 = nucleosomeVolumePositions->at(j).posSugarTMP2;

        // Variables for the cut solid volumes
        // residues
        G4VSolid* sugarTMP1;
        G4VSolid* sugarTHF1;
        G4VSolid* base1;
        G4VSolid* base2;
        G4VSolid* sugarTHF2;
        G4VSolid* sugarTMP2;

        // hydration shells
        // G4VSolid* sugarTMP1Water;
        // G4VSolid* sugarTHF1Water;
        // G4VSolid* base1Water;
        // G4VSolid* base2Water;
        // G4VSolid* sugarTHF2Water;
        // G4VSolid* sugarTMP2Water;


        // if cutVolumes is true it means we will run calculations so create the cut volumes
        // if(true)
        if(cutVolumes)
        {
            // residues
            sugarTMP1 = CreateCutSolid(solidSugarTMP,posSugarTMP1,posAndRadiusMap, "sugarTMP", true);
            sugarTHF1 = CreateCutSolid(solidSugarTHF,posSugarTHF1,posAndRadiusMap, "sugarTHF", true);
            base1 = CreateCutSolid(solidBase,posBase1,posAndRadiusMap, "base", true);
            base2 = CreateCutSolid(solidBase,posBase2,posAndRadiusMap, "base", true);
            sugarTHF2 = CreateCutSolid(solidSugarTHF,posSugarTHF2,posAndRadiusMap, "sugarTHF", true);
            sugarTMP2 = CreateCutSolid(solidSugarTMP,posSugarTMP2,posAndRadiusMap, "sugarTMP", true);

            // hydration shells
            // sugarTMP1Water = CreateCutSolid(solidSugarTMPWater,posSugarTMP1,posAndRadiusMap);
            // sugarTHF1Water = CreateCutSolid(solidSugarTHFWater,posSugarTHF1,posAndRadiusMap);
            // base1Water = CreateCutSolid(solidBaseWater,posBase1,posAndRadiusMap);
            // base2Water = CreateCutSolid(solidBaseWater,posBase2,posAndRadiusMap);
            // sugarTHF2Water = CreateCutSolid(solidSugarTHFWater,posSugarTHF2,posAndRadiusMap);
            // sugarTMP2Water = CreateCutSolid(solidSugarTMPWater,posSugarTMP2,posAndRadiusMap);
        }
        // if cutVolumes is false it means we just want to visualize the geometry so we do not need
        // the cutted volumes. Just assign the uncut solids to the empty variables.
        else
        {
            //residues
            sugarTMP1 = solidSugarTMP;
            sugarTHF1 = solidSugarTHF;
            base1 = solidBase;
            base2 = solidBase;
            sugarTHF2 = solidSugarTHF;
            sugarTMP2 = solidSugarTMP;

            // hydration shells
            // sugarTMP1Water = solidSugarTMPWater;
            // sugarTHF1Water = solidSugarTHFWater;
            // base1Water = solidBaseWater;
            // base2Water = solidBaseWater;
            // sugarTHF2Water = solidSugarTHFWater;
            // sugarTMP2Water = solidSugarTMPWater;
        }

        // G4double sugarVolume = sugarTMP1->GetCubicVolume();
        // G4cout << "**********************************************" << G4endl;
        // G4cout << "Sugar volume = " << sugarVolume/m3 << G4endl;
        // G4cout << "**********************************************" << G4endl;

        //------------------------------------------------------------------------------------------
        // Then: create logical volumes using the cut solids
        //------------------------------------------------------------------------------------------
        // residues
        G4LogicalVolume* logicSugarTHF1;
        G4LogicalVolume* logicSugarTMP1;
        G4LogicalVolume* logicBase1;
        G4LogicalVolume* logicBase2;
        G4LogicalVolume* logicSugarTHF2;
        G4LogicalVolume* logicSugarTMP2;
        // hydration shells
        // G4LogicalVolume* logicSugarTMP1Water;
        // G4LogicalVolume* logicSugarTHF1Water;
        // G4LogicalVolume* logicBase1Water;
        // G4LogicalVolume* logicBase2Water;
        // G4LogicalVolume* logicSugarTHF2Water;
        // G4LogicalVolume* logicSugarTMP2Water;

        // Creation of residues
        // logicSugarTMP1 = new G4LogicalVolume(sugarTMP1,fDNAMaterial,"logic_sugar_TMP_1");
        // logicSugarTHF1 = new G4LogicalVolume(sugarTHF1,fDNAMaterial,"logic_sugar_THF_1");
        // logicBase1 = new G4LogicalVolume(base1,fDNAMaterial,"logic_base_1"); // PY
        // logicBase2 = new G4LogicalVolume(base2,fDNAMaterial,"logic_base_2"); // PU
        // logicSugarTHF2 = new G4LogicalVolume(sugarTHF2,fDNAMaterial,"logic_sugar_THF_2");
        // logicSugarTMP2 = new G4LogicalVolume(sugarTMP2,fDNAMaterial,"logic_sugar_TMP_2");
        // G4cout << "fWaterName = " << fWaterName << G4endl;
        // G4cout << "fWater = " << fWater << G4endl;
        // G4cout << "fDNAMaterialName = " << fDNAMaterialName << G4endl;
        // G4cout << "fDNAMaterial = " << fDNAMaterial << G4endl;

        if (fUseG4Volumes) {
            logicSugarTMP1 = new G4LogicalVolume(sugarTMP1,fDNAMaterial,"logic_sugar_TMP_1");
            logicSugarTHF1 = new G4LogicalVolume(sugarTHF1,fDNAMaterial,"logic_sugar_THF_1");
            logicBase1 = new G4LogicalVolume(base1,fDNAMaterial,"logic_base_1"); // PY
            logicBase2 = new G4LogicalVolume(base2,fDNAMaterial,"logic_base_2"); // PU
            logicSugarTHF2 = new G4LogicalVolume(sugarTHF2,fDNAMaterial,"logic_sugar_THF_2");
            logicSugarTMP2 = new G4LogicalVolume(sugarTMP2,fDNAMaterial,"logic_sugar_TMP_2");
        }
        else {
            // Logical volumes for each nucleotide have different names
            // G4String testname = "logic_sugar_TMP_1"+std::to_string(j);
            // logicSugarTMP1 = CreateLogicalVolume(testname,fDNAMaterialName,sugarTMP1);
            // testname = "logic_sugar_THF_1"+std::to_string(j);
            // logicSugarTHF1 = CreateLogicalVolume(testname,fDNAMaterialName,sugarTHF1);
            // testname = "logic_base_1"+std::to_string(j);
            // logicBase1 = CreateLogicalVolume(testname,fDNAMaterialName,base1);
            // testname = "logic_base_2"+std::to_string(j);
            // logicBase2 = CreateLogicalVolume(testname,fDNAMaterialName,base2);
            // testname = "logic_sugar_THF_2"+std::to_string(j);
            // logicSugarTHF2 = CreateLogicalVolume(testname,fDNAMaterialName,sugarTHF2);
            // testname = "logic_sugar_TMP_2"+std::to_string(j);
            // logicSugarTMP2 = CreateLogicalVolume(testname,fDNAMaterialName,sugarTMP2);

            // Logical volumes for each nucleotide have the same name
            logicSugarTMP1 = CreateLogicalVolume("logic_sugar_TMP_1",fDNAMaterialName,sugarTMP1);
            logicSugarTHF1 = CreateLogicalVolume("logic_sugar_THF_1",fDNAMaterialName,sugarTHF1);
            logicBase1 = CreateLogicalVolume("logic_base_1",fDNAMaterialName,base1);
            logicBase2 = CreateLogicalVolume("logic_base_2",fDNAMaterialName,base2);
            logicSugarTHF2 = CreateLogicalVolume("logic_sugar_THF_2",fDNAMaterialName,sugarTHF2);
            logicSugarTMP2 = CreateLogicalVolume("logic_sugar_TMP_2",fDNAMaterialName,sugarTMP2);
        }  

        // Creation of hydration shells
        // logicSugarTMP1Water = new G4LogicalVolume(sugarTMP1Water,fDNAMaterial,"logic_sugarTMP_1_hydra");
        // logicSugarTHF1Water = new G4LogicalVolume(sugarTHF1Water,fDNAMaterial,"logic_sugarTHF_1_hydra");
        // logicBase1Water = new G4LogicalVolume(base1Water, fDNAMaterial,"logic_base_1_hydra");
        // logicBase2Water = new G4LogicalVolume(base2Water, fDNAMaterial,"logic_base_2_hydra");
        // logicSugarTHF2Water = new G4LogicalVolume(sugarTHF2Water,fDNAMaterial,"logic_sugarTHF_2_hydra");
        // logicSugarTMP2Water = new G4LogicalVolume(sugarTMP2Water,fDNAMaterial,"logic_sugarTMP_2_hydra");

        //------------------------------------------------------------------------------------------
        // Set visualization attributes for residues & hydration shell
        //------------------------------------------------------------------------------------------
        logicSugarTMP1->SetVisAttributes(visPhosphate);
        logicSugarTHF1->SetVisAttributes(visSugar);
        logicBase1->SetVisAttributes(visBase);
        logicBase2->SetVisAttributes(visBase);
        logicSugarTHF2->SetVisAttributes(visSugar);
        logicSugarTMP2->SetVisAttributes(visPhosphate);

        // logicSugarTMP1Water->SetVisAttributes(visHydration);
        // logicSugarTHF1Water->SetVisAttributes(visHydration);
        // logicBase1Water->SetVisAttributes(visHydration);
        // logicBase2Water->SetVisAttributes(visHydration);
        // logicSugarTHF2Water->SetVisAttributes(visHydration);
        // logicSugarTMP2Water->SetVisAttributes(visHydration);

        //------------------------------------------------------------------------------------------
        // Save the logical volumes in the output map
        //------------------------------------------------------------------------------------------
        (*logicSolidsMap)["sugarTMP1"].push_back(logicSugarTMP1);
        (*logicSolidsMap)["sugarTHF1"].push_back(logicSugarTHF1);
        (*logicSolidsMap)["base1"].push_back(logicBase1);
        (*logicSolidsMap)["base2"].push_back(logicBase2);
        (*logicSolidsMap)["sugarTHF2"].push_back(logicSugarTHF2);
        (*logicSolidsMap)["sugarTMP2"].push_back(logicSugarTMP2);

        // (*logicSolidsMap)["sugarTMP1Water"].push_back(logicSugarTMP1Water);
        // (*logicSolidsMap)["sugarTHF1Water"].push_back(logicSugarTHF1Water);
        // (*logicSolidsMap)["base1Water"].push_back(logicBase1Water);
        // (*logicSolidsMap)["base2Water"].push_back(logicBase2Water);
        // (*logicSolidsMap)["sugarTHF2Water"].push_back(logicSugarTHF2Water);
        // (*logicSolidsMap)["sugarTMP2Water"].push_back(logicSugarTMP2Water);
    } // complete iterating over all bp in single nucleotide
    
    // Note: each vector of the logicSolidsMap has 200 elements
    return logicSolidsMap;
}


//--------------------------------------------------------------------------------------------------
// Cut algorithm to avoid overlaps.
// Idea: we must have a reference and a target. The reference is the solid we are considering (
// described by parameters solidOrbRef & posRef) and which could be cut if an overlap is
// detected with the target solid. In a geometry, it implies we have to go through all the target 
// solids for a given reference solid. Target solid info (position and radius) is included in tarMap
// This method will return the cut spherical reference solid.
// Note: fifth parameter, i.e. G4bool in doesn't seem to be used.
//--------------------------------------------------------------------------------------------------
G4VSolid* TsFiberV2::CreateCutSolid(G4Orb *solidOrbRef,
                                       G4ThreeVector& posRef,
                                       std::map<G4ThreeVector,G4double>* tarMap,
                                       G4String volName,
                                       G4bool in)
{
    // return solidOrbRef;
    G4SubtractionSolid* solidCut(NULL); // container for the cut solid
    // G4UnionSolid* solidCut(NULL); // container for the cut solid

    bool isCutted = false; // flag to indicate if volume has been cut yet
    bool isOurVol = false; // flag to indicate if target volume is our reference volume

    //----------------------------------------------------------------------------------------------
    // Define radius of the reference solid we are focusing on
    //----------------------------------------------------------------------------------------------
    G4double radiusRef (0);
    // if(volName=="base") radiusRef = fBaseRadiusWater;
    // else if(volName=="sugarTHF") radiusRef = fSugarTHFRadiusWater;
    // else if(volName=="sugarTMP") radiusRef = fSugarTMPRadiusWater;
    if(volName=="base") radiusRef = fBaseRadius;
    else if(volName=="sugarTHF") radiusRef = fSugarTHFRadius;
    else if(volName=="sugarTMP") radiusRef = fSugarTMPRadius;
    // Hydration shell is handled if no volName provided
    else radiusRef = solidOrbRef->GetRadius();

    //----------------------------------------------------------------------------------------------
    // iterate on all the residue volumes in the map (3600 elements), i.e. "targets"
    //----------------------------------------------------------------------------------------------
    std::map<G4ThreeVector,G4double>::iterator it;
    std::map<G4ThreeVector,G4double>::iterator ite;
    G4int count = 0;

    for(it=tarMap->begin(), ite=tarMap->end();it!=ite;++it)
    {
        G4ThreeVector posTar = it->first; // position of target
        G4double radiusTar = it->second; // radius of target
        G4double distance = std::abs( (posRef-posTar).getR() ); // 3D distance between ref & target

        //------------------------------------------------------------------------------------------
        // Check if target volume = reference volume (can only happen once)
        //------------------------------------------------------------------------------------------
        if(distance == 0 && !isOurVol)
        {
            // G4cout << "The matching index is: " << count << G4endl;
            // target volume is the reference volume
            // G4cout << "Distance is 0 at index " << count << G4endl;
            // G4cout << "Radius = " << radiusTar << G4endl;
            // G4cout << "Position = " << posTar << G4endl;
            isOurVol = true;
        }
        //------------------------------------------------------------------------------------------
        // Throw error if position of target and reference match more than once. Implies there are 
        // two overlapping volumes at the same position.
        //------------------------------------------------------------------------------------------
        else if(distance == 0 && isOurVol)
        {
            G4cerr<<"DetectorConstruction::CreateCutSolid, Fatal Error. Two volumes are placed at the same position."<<G4endl;
            exit(EXIT_FAILURE);
        }
        //------------------------------------------------------------------------------------------
        // If the reference and target are overlapping, cut the reference. However, the target will
        // also be cut on another iteration of this loop. The cuts are performed at the "middle" 
        // point of intersection between them. The mathematics proceed according to that for
        // sphere-sphere intersections.
        //------------------------------------------------------------------------------------------
        else if(distance <= radiusRef+radiusTar)
        {
            // Solid volume used to cut. Make size "equal" to that of larger between the reference
            // and current target.
            G4double sliceBoxSize = std::max(radiusRef,radiusTar);
            G4Box* sliceBox = new G4Box("solid box for cut", sliceBoxSize, sliceBoxSize, sliceBoxSize);

            //--------------------------------------------------------------------------------------
            // To calculate the position of the intersection center
            //--------------------------------------------------------------------------------------
            // Displacement vector between target and reference
            G4ThreeVector displacement_vector = posTar - posRef;
            // Find the middle overlap point between the target and reference 
            G4double intersection = (pow(radiusRef,2)-pow(radiusTar,2)+pow(distance,2) ) / (2*distance) + sliceBox->GetZHalfLength();
            // Add small safety buffer
            intersection -= 0.001*fFactor*nm;
            // Create a vector to the intersection position, where one edge of the slicing volume
            // will be placed 
            G4ThreeVector posSlice = intersection *( displacement_vector/displacement_vector.getR() );

            //--------------------------------------------------------------------------------------
            // Calculate the necessary rotations.
            //--------------------------------------------------------------------------------------
            G4double phi = std::acos(posSlice.getZ()/posSlice.getR());
            G4double theta = std::acos( posSlice.getX() / ( posSlice.getR()*std::cos(M_PI/2.-phi) ) );

            if(posSlice.getY()<0) theta = -theta;

            G4ThreeVector rotAxisForPhi(1*fFactor*nm,0.,0.);
            rotAxisForPhi.rotateZ(theta+M_PI/2);
            G4RotationMatrix *rotMat = new G4RotationMatrix;
            rotMat->rotate(-phi, rotAxisForPhi);

            G4ThreeVector rotZAxis(0.,0.,1*fFactor*nm);
            rotMat->rotate(theta, rotZAxis);

            //--------------------------------------------------------------------------------------
            // Create the G4SubtractionSolid.
            //--------------------------------------------------------------------------------------
            if(!isCutted) solidCut = new G4SubtractionSolid("solidCut", solidOrbRef, sliceBox, rotMat, posSlice);
            else solidCut = new G4SubtractionSolid("solidCut", solidCut, sliceBox, rotMat, posSlice);
            // if(!isCutted) solidCut = new G4UnionSolid("solidCut", solidOrbRef, sliceBox, rotMat, posSlice);
            // else solidCut = new G4UnionSolid("solidCut", solidCut, sliceBox, rotMat, posSlice);

            isCutted = true;
        }
        count++;
    }

    if(isCutted) return solidCut;
    else return solidOrbRef;
}

//--------------------------------------------------------------------------------------------------
// Helper function used by buildLogicFiber() in verbose mode (i.e. if want to output the mean volume
// of cut residue and water volumes).
//--------------------------------------------------------------------------------------------------
void TsFiberV2::CalculateMeanVol(std::map<G4String, std::vector<G4LogicalVolume*> >* logicSolidsMap)
{
    G4double sugarTMP1Vol = 0;
    G4double sugarTHF1Vol = 0;
    G4double base1Vol = 0;
    G4double base2Vol = 0;
    G4double sugarTHF2Vol = 0;
    G4double sugarTMP2Vol = 0;

    G4double sugarTMP1WaterVol = 0;
    G4double sugarTHF1WaterVol = 0;

    G4double base1WaterVol = 0;
    G4double base2WaterVol = 0;
    G4double sugarTHF2WaterVol = 0;
    G4double sugarTMP2WaterVol = 0;

    // iterate on each bp
    for(int j=0;j<fBpNum;++j)
    {
        if(j==0) G4cout<<"Volume calculations\nThe fFactor value is taken into account already. We assume fFactor=1e+9."<<G4endl;
        // assert(fFactor==1e+9);
        sugarTMP1Vol += logicSolidsMap->at("sugarTMP1")[j]->GetSolid()->GetCubicVolume();
        sugarTHF1Vol += logicSolidsMap->at("sugarTHF1")[j]->GetSolid()->GetCubicVolume();
        base1Vol += logicSolidsMap->at("base1")[j]->GetSolid()->GetCubicVolume();
        base2Vol += logicSolidsMap->at("base2")[j]->GetSolid()->GetCubicVolume();
        sugarTHF2Vol += logicSolidsMap->at("sugarTHF2")[j]->GetSolid()->GetCubicVolume();
        sugarTMP2Vol += logicSolidsMap->at("sugarTMP2")[j]->GetSolid()->GetCubicVolume();

        // sugarTMP1WaterVol += logicSolidsMap->at("sugarTMPWater1")[j]->GetSolid()->GetCubicVolume();
        // sugarTHF1WaterVol += logicSolidsMap->at("sugarTHFWater1")[j]->GetSolid()->GetCubicVolume();
        // base1WaterVol += logicSolidsMap->at("baseWater1")[j]->GetSolid()->GetCubicVolume();
        // base2WaterVol += logicSolidsMap->at("baseWater2")[j]->GetSolid()->GetCubicVolume();
        // sugarTHF2WaterVol += logicSolidsMap->at("sugarTHFWater2")[j]->GetSolid()->GetCubicVolume();
        // sugarTMP2WaterVol += logicSolidsMap->at("sugarTMPWater2")[j]->GetSolid()->GetCubicVolume();
    }

    sugarTMP1Vol = sugarTMP1Vol/(fBpNum);
    sugarTHF1Vol = sugarTHF1Vol/(fBpNum);
    base1Vol = base1Vol/(fBpNum);
    base2Vol = base2Vol/(fBpNum);
    sugarTHF2Vol = sugarTHF2Vol/(fBpNum);
    sugarTMP2Vol = sugarTMP2Vol/(fBpNum);

    // sugarTMP1Vol = sugarTMP1Vol/(fBpNum*pow(fFactor,3) );
    // sugarTHF1Vol = sugarTHF1Vol/(fBpNum*pow(fFactor,3) );
    // base1Vol = base1Vol/(fBpNum*pow(fFactor,3));
    // base2Vol = base2Vol/(fBpNum*pow(fFactor,3));
    // sugarTHF2Vol = sugarTHF2Vol/(fBpNum*pow(fFactor,3));
    // sugarTMP2Vol = sugarTMP2Vol/(fBpNum*pow(fFactor,3));

    // sugarTMP1WaterVol = sugarTMP1WaterVol/(fBpNum*pow(fFactor,3) ) - sugarTMP1Vol;
    // sugarTHF1WaterVol = sugarTHF1WaterVol/(fBpNum*pow(fFactor,3) ) - sugarTHF1Vol;
    // base1WaterVol = base1WaterVol/(fBpNum*pow(fFactor,3)) - base1Vol;
    // base2WaterVol = base2WaterVol/(fBpNum*pow(fFactor,3)) - base2Vol;
    // sugarTHF2WaterVol = sugarTHF2WaterVol/(fBpNum*pow(fFactor,3)) - sugarTHF2Vol;
    // sugarTMP2WaterVol = sugarTMP2WaterVol/(fBpNum*pow(fFactor,3)) - sugarTMP2Vol;

    G4cout<<"\nsugarTMP1Vol="<<sugarTMP1Vol/m3*1e+27<<" nm3"<<G4endl;
    G4cout<<"\nsugarTHF1Vol="<<sugarTHF1Vol/m3*1e+27<<" nm3"<<G4endl;
    G4cout<<"base1Vol="<<base1Vol/m3*1e+27<<" nm3"<<G4endl;
    G4cout<<"base2Vol="<<base2Vol/m3*1e+27<<" nm3"<<G4endl;
    G4cout<<"sugarTHF2Vol="<<sugarTHF2Vol/m3*1e+27<<" nm3\n"<<G4endl;
    G4cout<<"sugarTMP2Vol="<<sugarTMP2Vol/m3*1e+27<<" nm3\n"<<G4endl;

    // G4cout<<"\nsugarTMP1WaterVol="<<sugarTMP1WaterVol/m3*1e+27<<" nm3"<<G4endl;
    // G4cout<<"\nsugarTHF1WaterVol="<<sugarTHF1WaterVol/m3*1e+27<<" nm3"<<G4endl;
    // G4cout<<"base1WaterVol="<<base1WaterVol/m3*1e+27<<" nm3"<<G4endl;
    // G4cout<<"base2WaterVol="<<base2WaterVol/m3*1e+27<<" nm3"<<G4endl;
    // G4cout<<"sugarTHF2WaterVol="<<sugarTHF2WaterVol/m3*1e+27<<" nm3\n"<<G4endl;
    // G4cout<<"sugarTMP2WaterVol="<<sugarTMP2WaterVol/m3*1e+27<<" nm3\n"<<G4endl;
}



    // //**********************************************************************************************
    // // Verification of cutting solid behaviour
    // //----------------------------------------------------------------------------------------------
    // // Sphere 1 & 2 solids & logicals 
    // //----------------------------------------------------------------------------------------------
    // // G4double radius1 = 10*nm;
    // // G4double radius2 = 5*nm;
    // G4double radius1 = 0.27*nm;
    // G4double radius2 = 0.29*nm;

    // G4Orb* sphere1Solid = new G4Orb("sphere1",radius1);
    // G4Orb* sphere2Solid = new G4Orb("sphere2",radius2);

    // G4LogicalVolume* sphere1Log = CreateLogicalVolume("log1", sphere1Solid);
    // G4LogicalVolume* sphere2Log = CreateLogicalVolume("log2", sphere2Solid);

    // G4VisAttributes sphere1Vis(G4Colour(0.0, 1.0, 0.0, 0.3) );
    // sphere1Vis.SetVisibility(true);
    // sphere1Vis.SetForceSolid(true);
    // sphere1Log->SetVisAttributes(sphere1Vis);

    // G4VisAttributes sphere2Vis(G4Colour(0.0, 0.0, 1.0, 0.3) );
    // sphere2Vis.SetVisibility(true);
    // sphere2Vis.SetForceSolid(true);
    // sphere2Log->SetVisAttributes(sphere2Vis);


    // //----------------------------------------------------------------------------------------------
    // // Various ways of doing positioning - uncomment diff pos2 settings to test 
    // //----------------------------------------------------------------------------------------------
    // // G4ThreeVector pos1 = G4ThreeVector(0.,0.,0.);
    // // G4ThreeVector pos2 = G4ThreeVector(0.,0.,radius1+(radius2/2));
    // // G4ThreeVector pos2 = G4ThreeVector(0.,(radius1+(radius2/2))/sqrt(2),(radius1+(radius2/2))/sqrt(2));
    // // G4ThreeVector pos2 = G4ThreeVector((radius1+(radius2/2))/sqrt(2),(radius1+(radius2/2))/sqrt(2),0.);
    // // G4ThreeVector pos2 = G4ThreeVector((radius1+(radius2/2))/sqrt(3),(radius1+(radius2/2))/sqrt(3),(radius1+(radius2/2))/sqrt(3));

    // // other octants:
    // // G4ThreeVector pos2 = G4ThreeVector(0.,0.,-1*(radius1+(radius2/2)));
    // // G4ThreeVector pos2 = G4ThreeVector(0.,-1*(radius1+(radius2/2))/sqrt(2),(radius1+(radius2/2))/sqrt(2));
    // // G4ThreeVector pos2 = G4ThreeVector(-(radius1+(radius2/2))/sqrt(3),-(radius1+(radius2/2))/sqrt(3),-(radius1+(radius2/2))/sqrt(3));

    // // other angles 
    // // G4ThreeVector pos2 = G4ThreeVector(0,radius2/2,radius1+(radius2/4));

    // // match actual sim
    // G4ThreeVector pos1 = G4ThreeVector(7.86727*nm,13.2035*nm,1.2072*nm);
    // G4ThreeVector pos2 = G4ThreeVector(7.8662*nm,13.1051*nm,1.5331*nm);


    // //----------------------------------------------------------------------------------------------
    // // offset positions from origin (include a sphere @ origin for scale)
    // //----------------------------------------------------------------------------------------------
    // G4double deltaX = 0*nm;
    // G4double deltaY = 0*nm;
    // G4double deltaZ = 0*nm;
    // G4Orb* originSolid = new G4Orb("sphere1",0.5*nm);
    // G4LogicalVolume* originLog = CreateLogicalVolume("originLog", originSolid);
    // G4VisAttributes originVis(G4Colour(1.0, 1.0, 1.0) );
    // sphere1Vis.SetVisibility(true);
    // sphere1Vis.SetForceSolid(true);
    // sphere1Log->SetVisAttributes(sphere1Vis);
    // G4RotationMatrix* rotOrigin = new G4RotationMatrix(0,0,0);
    // G4ThreeVector posOrigin = G4ThreeVector(0.,0.,0.);
    // // CreatePhysicalVolume("originPhys", originLog, rotOrigin, &posOrigin, fEnvelopePhys);

    // pos1.setX(pos1.getX()+deltaX);
    // pos1.setY(pos1.getY()+deltaY);
    // pos1.setZ(pos1.getZ()+deltaZ);

    // pos2.setX(pos2.getX()+deltaX);
    // pos2.setY(pos2.getY()+deltaY);
    // pos2.setZ(pos2.getZ()+deltaZ);

    // //----------------------------------------------------------------------------------------------
    // // rotation matrices for spheres 1 & 2 (should be zero)
    // //----------------------------------------------------------------------------------------------
    // G4RotationMatrix* rot1 = new G4RotationMatrix(0,0,0);
    // G4RotationMatrix* rot2 = new G4RotationMatrix(0,0,0);


    // //----------------------------------------------------------------------------------------------
    // // solid & logical volumes for the slicing box
    // //----------------------------------------------------------------------------------------------
    // G4Box* sliceBox = new G4Box("cutoff_box",2*radius2, 2*radius2, 2*radius2);

    // // Positioning information for sliceBox
    // G4ThreeVector displacement_vector = pos2 - pos1;
    // G4double displacement = displacement_vector.getR();
    // // G4double crossover = ((pow(radius1,2) + pow(displacement,2) - pow(radius2,2))/(2*displacement)) + sliceBox->GetZHalfLength();
    // G4double crossover = ((pow(radius1,2) + pow(displacement,2) - pow(radius2,2))/(2*displacement)) + sliceBox->GetZHalfLength() - 0.001*nm;;
    // G4ThreeVector posSlice = crossover * (displacement_vector/displacement);

    // G4LogicalVolume* sliceLog = CreateLogicalVolume("sliceLog", sliceBox);
    // G4VisAttributes sliceVis(G4Colour(1.0, 0.0, 0.0, 0.3) );
    // sliceVis.SetVisibility(true);
    // sliceVis.SetForceSolid(true);
    // sliceLog->SetVisAttributes(sliceVis);

    // //----------------------------------------------------------------------------------------------
    // // My first attempt at rotating the slicing box
    // //----------------------------------------------------------------------------------------------
    // // G4double theta = std::atan(displacement_vector.getY()/displacement_vector.getX());
    // // G4double phi = std::acos(displacement_vector.getZ()/displacement);
    // // G4RotationMatrix* rotSlice = new G4RotationMatrix(theta,phi,0);
    // // G4RotationMatrix* rotSlice = new G4RotationMatrix(displacement_vector,phi);

    // //----------------------------------------------------------------------------------------------
    // // rotation of the slicing box
    // //----------------------------------------------------------------------------------------------
    // G4double phi = std::acos(posSlice.getZ()/posSlice.getR());
    // G4double theta = std::acos( posSlice.getX() / ( posSlice.getR()*std::cos(M_PI/2.-phi) ) );

    // if(posSlice.getY()<0) theta = -theta;

    // G4ThreeVector rotAxisForPhi(1*nm,0.,0.);
    // rotAxisForPhi.rotateZ(theta+M_PI/2);
    // G4RotationMatrix *rotMat = new G4RotationMatrix;
    // rotMat->rotate(-phi, rotAxisForPhi);

    // G4ThreeVector rotZAxis(0.,0.,1*nm);
    // rotMat->rotate(theta, rotZAxis);

    // //----------------------------------------------------------------------------------------------
    // // Create a union solid
    // //----------------------------------------------------------------------------------------------
    // G4UnionSolid* unionSolid = new G4UnionSolid("union",sphere1Solid,sliceBox,rotMat,posSlice);
    // G4LogicalVolume* unionLog = CreateLogicalVolume("unionLog", unionSolid);
    // unionLog->SetVisAttributes(sphere1Vis);

    // // Dont apply positioning offset from origin for slicing box until after rotation has been done
    // // and a union solid has been made
    // posSlice.setX(posSlice.getX()+deltaX);
    // posSlice.setY(posSlice.getY()+deltaY);
    // posSlice.setZ(posSlice.getZ()+deltaZ);


    // //----------------------------------------------------------------------------------------------
    // // Second slicing box
    // //----------------------------------------------------------------------------------------------
    // // G4Box* sliceBox2 = new G4Box("cutoff_box",2*radius2, 2*radius2, 2*radius2);

    // // // Positioning information for sliceBox
    // // G4ThreeVector displacement_vector2 = pos1 - pos2;
    // // G4double displacement2 = displacement_vector2.getR();
    // // // G4double crossover = ((pow(radius1,2) + pow(displacement,2) - pow(radius2,2))/(2*displacement)) + sliceBox->GetZHalfLength();
    // // G4double crossover2 = ((pow(radius2,2) + pow(displacement2,2) - pow(radius1,2))/(2*displacement2)) + sliceBox2->GetZHalfLength() - 0.001*nm;;
    // // G4ThreeVector posSlice2 = crossover2 * (displacement_vector2/displacement2);

    // // G4LogicalVolume* sliceLog2 = CreateLogicalVolume("sliceLog2", sliceBox);
    // // G4VisAttributes sliceVis2(G4Colour(1.0, 0.0, 0.0, 0.3) );
    // // sliceVis2.SetVisibility(true);
    // // sliceVis2.SetForceSolid(true);
    // // sliceLog2->SetVisAttributes(sliceVis2);

    // // G4double phi2 = std::acos(posSlice2.getZ()/posSlice2.getR());
    // // G4double theta2 = std::acos( posSlice2.getX() / ( posSlice2.getR()*std::cos(M_PI/2.-phi) ) );

    // // if(posSlice2.getY()<0) theta2 = -theta2;

    // // G4ThreeVector rotAxisForPhi2(1*nm,0.,0.);
    // // rotAxisForPhi2.rotateZ(theta2+M_PI/2);
    // // G4RotationMatrix *rotMat2 = new G4RotationMatrix;
    // // rotMat2->rotate(-phi2, rotAxisForPhi2);

    // // G4ThreeVector rotZAxis2(0.,0.,1*nm);
    // // rotMat2->rotate(theta2, rotZAxis2);

    // // G4UnionSolid* unionSolid2 = new G4UnionSolid("union2",sphere2Solid,sliceBox2,rotMat2,posSlice2);
    // // G4LogicalVolume* unionLog2 = CreateLogicalVolume("unionLog2", unionSolid2);
    // // unionLog2->SetVisAttributes(sphere2Vis);

    // // posSlice2.setX(posSlice2.getX()+deltaX);
    // // posSlice2.setY(posSlice2.getY()+deltaY);
    // // posSlice2.setZ(posSlice2.getZ()+deltaZ);

    // //----------------------------------------------------------------------------------------------
    // // Create & place physical volumes
    // //----------------------------------------------------------------------------------------------
    // // CreatePhysicalVolume("phys1", sphere1Log, rot1, &pos1, fEnvelopePhys);
    // // CreatePhysicalVolume("phys2", sphere2Log, rot2, &pos2, fEnvelopePhys);

    // // CreatePhysicalVolume("slice", sliceLog, rotSlice, &posSlice, fEnvelopePhys);
    // // CreatePhysicalVolume("slice", sliceLog, rotMat, &posSlice, fEnvelopePhys);

    // // Topas way of doing physical volumes (1 union, 1 normal)
    // CreatePhysicalVolume("union", unionLog, rot1, &pos1, fEnvelopePhys);
    // CreatePhysicalVolume("phys2", sphere2Log, rot2, &pos2, fEnvelopePhys);

    // // G4 way of doing physical volumes instead of topas:
    // // G4PVPlacement* phys1 = new G4PVPlacement(rot1,pos1,unionLog,"phys1",fEnvelopeLog,false,0);
    // // G4PVPlacement* phys2 = new G4PVPlacement(rot2,pos2,sphere2Log,"phys2",fEnvelopeLog,false,0);

    // // Create physical volume for second union volume:
    // // CreatePhysicalVolume("union2", unionLog2, rot2, &pos2, fEnvelopePhys);
    // //**********************************************************************************************







// // G4Tubs* boxOuter = new G4Tubs("outer_box", 0., 11*nm, 20*nm, 0., 360);
// G4Box* boxOuter = new G4Box("outer_box",20*nm,20*nm,20*nm);
// // G4Orb* boxOuter = new G4Orb("outer_box",10*nm);

// G4Box* boxInner = new G4Box("inner_box",20*nm,20*nm,10*nm);
// // G4Orb* boxInner = new G4Orb("outer_box",5*nm);
// // G4Box* boxInner = new G4Box("inner_box",20*nm,20*nm,5*nm);

// G4RotationMatrix *rotMat = new G4RotationMatrix;
// G4SubtractionSolid* boxSubtract = new G4SubtractionSolid("subtraction_box",boxOuter,boxInner,0,G4ThreeVector(0.,0.,10*nm));
// // G4UnionSolid* boxSubtract = new G4UnionSolid("union_box",boxOuter,boxInner,0,G4ThreeVector(0.,0.,10*nm));
// // G4Material* fWater = G4NistManager::Instance()->FindOrBuildMaterial("G4_WATER");

// // G4LogicalVolume* logicalBox = new G4LogicalVolume(boxSubtract,fWater,"boxOuter");
// // G4PVPlacement* sTMP1 = new G4PVPlacement(0,G4ThreeVector(0.,0.,0.),logicalBox,
// //     "physical_box",fEnvelopeLog,false,1);


// G4LogicalVolume* logicalBox = CreateLogicalVolume("logical_box",boxSubtract);
// G4VisAttributes boxVis(G4Colour(0.0, 1.0, 0.0, 0.5) );
// boxVis.SetVisibility(true);
// boxVis.SetForceSolid(true);
// // boxVis.SetForceWireframe(true);
// logicalBox->SetVisAttributes(boxVis);

// G4double subtractVolume = boxSubtract->GetCubicVolume();
// G4double outerVolume = boxOuter->GetCubicVolume();
// G4double innerVolume = boxInner->GetCubicVolume();
// G4cout << "---------------------------------------------------------" << G4endl;
// G4cout << "Outer volume: " << outerVolume/m3 << G4endl;
// G4cout << "Inner volume: " << innerVolume/m3 << G4endl;
// G4cout << "Subtracted volume: " << subtractVolume/m3 << G4endl;
// G4cout << "---------------------------------------------------------" << G4endl;

// CreatePhysicalVolume("physical_box", logicalBox, fEnvelopePhys);
