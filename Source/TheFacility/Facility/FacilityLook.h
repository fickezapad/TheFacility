// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 *  The retro look: low-poly pieces from the engine's basic shapes, dressed in the project's generated materials
 *  (Saved/QA/make_textures.py and make_materials.py write them to /Game/Facility/Materials).
 *
 *  Everything here is cosmetic. Pieces never collide, never affect navigation and never replicate: each machine builds the
 *  same ones from the same class defaults.
 */
namespace FacilityLook
{
	/** Constructors only. An engine basic shape by name: Cube, Sphere, Cylinder, Cone, Plane */
	UStaticMesh* Shape(const TCHAR* Name);

	/** Constructors only. A material from /Game/Facility/Materials by asset name, e.g. MI_Retro_Door */
	UMaterialInterface* Material(const TCHAR* Name);

	/**
	 *  Constructors only. A decorative piece on Parent (at Socket, if given): no collision, no navigation, no overlaps.
	 *  Location and rotation are relative to the parent (or the socket); Scale is relative to the parent too, unless
	 *  bAbsoluteScale, when it is the piece's world scale, whatever the parent is scaled to.
	 */
	UStaticMeshComponent* AddPiece(AActor* Owner, FName Name, USceneComponent* Parent, FName Socket, const TCHAR* ShapeName,
		const TCHAR* MaterialName, const FVector& Location, const FRotator& Rotation, const FVector& Scale, bool bAbsoluteScale = false);
}
