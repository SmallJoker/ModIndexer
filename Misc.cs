using System;
using System.Text;

namespace ModIndexer
{
	public static class Misc
	{
		// Including the forum number as value
		public enum FETCH_TYPE
		{
			REL_MODS  = 11,
			WIP_MODS  =  9,
			REL_GAMES = 15,
			WIP_GAMES = 50,
			OLD_MODS  = 13,
			CSM_MODS  = 53
		}

		// The number in the database for the type
		public enum DATA_TYPE
		{
			INVALID  = 0,
			REL_MOD  = 1,
			REL_MP   = 2,
			WIP_MOD  = 3,
			WIP_MP   = 4,
			OLD_MOD  = 5,
			REL_GAME = 6,
			WIP_GAME = 7,
			REL_CSM  = 8,
			WIP_CSM  = 9
		}

		public static DATA_TYPE getDataType(string text)
		{
			switch (text.ToLower()) {
			case "mod": return DATA_TYPE.REL_MOD;
			case "mod pack":
			case "modpack": return DATA_TYPE.REL_MP;
			case "game": return DATA_TYPE.REL_GAME;
			case "csm":
			case "clientmod":
			case "client mod": return DATA_TYPE.REL_CSM;
			case "old clientmod":  // TODO
			case "old client mod": // TODO
			case "old modpack":    // TODO
			case "old mod pack":   // TODO
			case "old mod": return DATA_TYPE.OLD_MOD;
			}
			return DATA_TYPE.INVALID;
		}

		// Convert special characters to HTML code
		public static string EscapeXML(this string t)
		{
			char[] fromCr = { '"', '\'', '\\', '{', '}', '|', '%', ':', '<', '>' };
			string[] toStr = { "&quot;", "&#39;", "&#92;", "&#123;", "&#125;", "&#124;", "&#37;", "&#58;", "&lt;", "&gt;" };
			StringBuilder sb = new StringBuilder();

			bool wasSpace = true;
			for (int i = 0; i < t.Length; i++) {
				char cur = t[i];

				bool isSpace = char.IsWhiteSpace(cur);
				if (wasSpace && isSpace)
					continue;

				// Cut off non-ASCII
				if ((ushort)t[i] > 0xFF)
					continue;

				bool found = false;
				for (int k = 0; k < fromCr.Length; k++) {
					if (cur == fromCr[k]) {
						sb.Append(toStr[k]);
						found = true;
						break;
					}
				}

				wasSpace = isSpace;
				if (!found)
					sb.Append(cur);
			}

			return sb.ToString();
		}

		public static int IndexOf<T>(this T[] me, T value)
		{
			for (int i = 0; i < me.Length; i++)
				if (me[i].Equals(value))
					return i;

			return -1;
		}
	}

	class ForumData
	{
		public int topicId, userId, type;
		public string title, userName, link;

		public ForumData(int topicId, string title, int type, 
			int userId, string userName, string link)
		{
			this.topicId  = topicId;
			this.title    = title;
			this.type     = type;
			this.userId   = userId;
			this.userName = userName;
			this.link     = link;
		}
	}
}
