// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComboTreeAsset.h"

const FComboNode* UComboTreeAsset::FindNode(FName NodeId) const
{
	for (const FComboNode& Node : Nodes)
	{
		if (Node.NodeId == NodeId)
		{
			return &Node;
		}
	}
	return nullptr;
}