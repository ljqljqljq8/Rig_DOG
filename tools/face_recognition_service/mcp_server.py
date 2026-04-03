from __future__ import annotations

from pathlib import Path

from mcp.server.fastmcp import FastMCP

from app import database


mcp = FastMCP("RIG Puppy Face Database")


@mcp.tool()
def list_known_people() -> dict:
    """List the names currently available in the local face database."""
    database.load()
    return {
        "success": True,
        "people": database.people(),
        "known_embeddings": len(database.known_faces),
    }


@mcp.tool()
def rebuild_face_database() -> dict:
    """Rebuild the local face database after photos are added, removed, or updated."""
    database.rebuild()
    return {
        "success": True,
        "people": database.people(),
        "known_embeddings": len(database.known_faces),
    }


@mcp.tool()
def recognize_image_file(image_path: str) -> dict:
    """Debug the local recognizer by matching a local image file against the local face database."""
    database.load()
    payload = Path(image_path).read_bytes()
    result = database.recognize(payload)
    result["image_path"] = image_path
    return result


if __name__ == "__main__":
    database.load()
    mcp.run(transport="stdio")
