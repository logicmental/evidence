namespace Evidence;

public sealed record NoteItem(
	string RelativePath,
	string Title,
	bool IsEncrypted = false,
	bool IsAttachment = false,
	string TagsDisplay = "");