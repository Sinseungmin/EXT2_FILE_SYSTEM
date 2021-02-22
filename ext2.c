typedef struct
{
	char* address;
} DISK_MEMORY;

#include "ext2.h"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 파일 쓰기
int ext2_write(EXT2_NODE* file, unsigned long offset, unsigned long length, const char* buffer)
{
	BYTE blockBuffer[MAX_BLOCK_SIZE];					 //섹터크기만큼 배열 크기 설정		// sector[MAX_SECTOR_SIZE] -> blockBuffer[MAX_BLOCK_SIZE]	by seungmin
	DWORD currentOffset, currentBlock, blockSeq = 0;
	DWORD blockNumber, blockOffset;						 // sectorNumber, sectorOffset - removed	/ blockOffset - added 	 by seungmin	 
	DWORD readEnd;
	QWORD blockSize;
	INODE node;
	int i;

	get_inode(file->fs, file->entry.inode, &node); // 작성할 파일의 아이노드 메타데이터를 node에 저장
	currentBlock = node.block[0];				   // 기록할 블록 번호
	readEnd = offset + length;					   // 쓰고자 하는 마지막 위치를 계산

	currentOffset = offset; // 기록할 offset

	blockSize = MAX_BLOCK_SIZE;

	i = 1;
	while (offset > blockSize) //기록할 위치 찾는 루프
	{
		currentBlock = get_data_block_at_inode(file->fs, node, ++i); // node의 i번째 데이터블록 번호
		blockSize += blockSize;
		blockSeq++;
	}

	while (currentOffset < readEnd) // 현재 offset이 쓰고자 하는 마지막 위치보다 앞쪽인동안
	{
		DWORD copyLength;

		blockNumber = currentOffset / MAX_BLOCK_SIZE; // 몇 번째 블록인지 계산

		if (currentBlock == 0 || currentBlock == inode_data_empty) // currentBlock이 할당되지 않은 경우
		{
			if (expand_block(file->fs, file->entry.inode) == EXT2_ERROR) //file->entry.inode 에 해당하는 아이노드를 찾아서 해당 아이노드의 비어있는 블록에 새로운 데이터 블록 할당.
				return EXT2_ERROR;
			// process_meta_data_for_block_used(file->fs, file->entry.inode, 0); //프로세스가 처리하다의 프로세스 같다. expand_block 에서 데이터 블록을 할당이나 해제 했을때, free_block_count나 block_bitmap같은 메타 데이터 수정
			get_inode(file->fs, file->entry.inode, &node);				   // file->entry.inode의 메타데이터를 node에 write
			currentBlock = node.block[0];								   // currentBlock을 할당된 데이터블록 번호로 지정
		}

		if (blockSeq != blockNumber) //다음 데이터 블록으로 넘어감
		{
			DWORD nextBlock;
			blockSeq++; // 몇번째 블록까지 읽었는지 저장하는 변수도 증가
			++i;
			nextBlock = get_data_block_at_inode(file->fs, node, i); // node의 i번째 데이터블록 번호를 리턴
			if (nextBlock == 0 || nextBlock == inode_data_empty)										// nextBlock이 할당되지 않은 경우
			{
				expand_block(file->fs, file->entry.inode);					   // 데이터블록 할당
				// process_meta_data_for_block_used(file->fs, file->entry.inode, 0);

				get_inode(file->fs, file->entry.inode, &node);				   // file->entry.inode의 메타데이터를 node에 write
				nextBlock = get_data_block_at_inode(file->fs, node, i); // node의 i번째 데이터블록 번호를 리턴

				if (nextBlock == 0 || nextBlock == inode_data_empty) // nextBlock이 또 할당되지 않은 경우
				{
					return EXT2_ERROR;
				}
			}
			currentBlock = nextBlock; // 다음 블록으로 이동
		}
		/*
		sectorNumber = (currentOffset / (MAX_SECTOR_SIZE)) % (MAX_BLOCK_SIZE / MAX_SECTOR_SIZE); // 몇 번째 섹터인지 계산
		sectorOffset = currentOffset % MAX_SECTOR_SIZE;											 // 섹터 내의 위치 계산
		by seungmin*/

		blockOffset = currentOffset % MAX_BLOCK_SIZE;		// by seungmin

		copyLength = MIN(MAX_BLOCK_SIZE - blockOffset, readEnd - currentOffset); // 섹터에서 읽어온 바이트 수와 더 읽어야 할 바이트 수 중 작은 값을 버퍼로 복사할 크기로 정함
		// from (MAX_SECTOR_SIZE - sectorOffset)	by seungmin

		if (copyLength != MAX_BLOCK_SIZE) // 복사할 크기가 섹터 하나의 크기와 같이 않으면
										   // -> 한 섹터의 내용을 모두 바꿀거면 굳이 읽어올 필요가 없지만
										   //    일부만 바꿀 경우 섹터 단위로 읽어오지 않고 데이터를 쓸 경우 나머지 데이터가 원하지 않는 값으로 변경될 수 있음
		{
			if (block_read(file->fs, 0, currentBlock, blockBuffer)) //두번째 인자가 0일때는 그룹이 하나 일때, 여러개면 지금 있는게 맞다.
				break;										   // file->fs->sb.block_group_number -> 0		by seungmin
		}

		memcpy(&blockBuffer[blockOffset], buffer, copyLength); // 버퍼에서 새로 쓸 데이터 복사	// &sector[sectorOffset] -> &blockBuffer[blockOffset]  by seungmin

		if (block_write(file->fs, 0, currentBlock, blockBuffer))		// sector -> blockBuffer   by seungmin
			break;

		buffer += copyLength;
		currentOffset += copyLength;
	}

	node.size = MAX(currentOffset, node.size); // 새로 쓴 영역과 기존 파일의 크기 중 큰 값을 파일 크기로 설정
	set_inode_onto_inode_table(file->fs, file->entry.inode, &node); // 아이노드 테이블 업데이트

	return currentOffset - offset;
}

UINT32 get_free_inode_number(EXT2_FILESYSTEM* fs);

// (eunseo)
void process_meta_data_for_inode_used(EXT2_NODE* retEntry, UINT32 inode_num, int fileType)
{
	/*
	아이노드를 할당, 해제했을 때 free_inode_count, inode_bitmap 등의 메타데이터를 알맞게 수정하는 함수
	마찬가지로 아이노드를 수정한 후 이 함수를 호출할 경우 이 함수에서 수정된 아이노드의 번호를 가지고 수정내역을 다시 탐색 후
	메타데이터를 수정해야 할 것 같은데 그럴 바에는 애초에 아이노드 할당이나 해제하는 함수에서 이 작업도 같이하는 편이 나을 듯
	세 번째 인자일 fileType은 파일이 디렉토리인지 아닌지 등을 판단해 메타데이터 수정 시 사용할 듯
	*/
	// 일단은 사용되었을 때(할당)를 가정. 해제는 고려하지 않음
	INODE	inodeBuffer;
	BYTE	sector[MAX_BLOCK_SIZE];
	UINT32	i, offset;
	BYTE	mask = 1;
	UINT32	groupNum = GET_INODE_GROUP(inode_num);

	if (get_inode(retEntry->fs, inode_num, &inodeBuffer) == EXT2_ERROR) // inode number에 대한 메타데이터를 inodeBuffer에 저장
		return;

	retEntry->entry.inode = inode_num; // retEntry의 아이노드 번호를 inode_num으로 지정
	inodeBuffer.mode = fileType; // file type 지정
	set_inode_onto_inode_table(retEntry->fs, inode_num, &inodeBuffer); // 아이노드 테이블 업데이트

	// 디스크의 super block과 group descriptor 수정
	for (i = 0; i < NUMBER_OF_GROUPS; i++)
	{
		// 디스크의 sb.free_inode_count를 1 감소
		ZeroMemory(sector, sizeof(sector));
		block_read(retEntry->fs, i, SUPER_BLOCK, sector);
		((EXT2_SUPER_BLOCK*)sector)->free_inode_count--;
		block_write(retEntry->fs, i, SUPER_BLOCK, sector);

		// 디스크의 gd.free_inodes_count 1 감소
		ZeroMemory(sector, sizeof(sector));
		block_read(retEntry->fs, i, GROUP_DES, sector);
		((EXT2_GROUP_DESCRIPTOR*)sector)[groupNum].free_inodes_count--;
		block_write(retEntry->fs, i, GROUP_DES, sector);
	}
	printf("inode_num : %d\n", inode_num);
	printf("groupNum : %d\n", groupNum);
	retEntry->fs->sb.free_inode_count--; // fs의 sb.free_inode_count를 1 감소
	if(0 == groupNum)
		retEntry->fs->gd.free_inodes_count--; // fs의 gd.free_blocks_count를 1 감소

	if (groupNum == 1)
		printf("%d\n", groupNum);
	// Update inode bitmap
	ZeroMemory(sector, MAX_BLOCK_SIZE);
	block_read(retEntry->fs, groupNum, retEntry->fs->gd.start_block_of_inode_bitmap, sector); // 아이노드 비트맵 blockBuffer 버퍼에 저장
	offset = (inode_num - 1) % 8; // 섹터 내의 offset 계산
	mask <<= offset; // 오프셋을 1로 수정하기 위한 마스크
	sector[((inode_num - 1) % (retEntry->fs->sb.inode_per_group)) / 8] |= mask; // 비트맵 수정
	block_write(retEntry->fs, groupNum, retEntry->fs->gd.start_block_of_inode_bitmap, sector); // 디스크에 수정된 비트맵 저장
	
	return;
}

// 디스크의 location에 value를 기록 (eunseo)
int set_entry(EXT2_FILESYSTEM* fs, const EXT2_DIR_ENTRY_LOCATION* location, const EXT2_DIR_ENTRY* value)
{
	/* value의 크기가 섹터만큼 크지 않더라도 바로 기록하면 나머지 정보들이 원하지 않는 값으로 바뀔 수 있기 때문에
	섹터 단위로 읽어와 해당 영역만 바꿔준 후 다시 기록함 */
	BYTE	blockBuffer[MAX_BLOCK_SIZE];
	EXT2_DIR_ENTRY* entry;

	block_read(fs, location->group, location->block, blockBuffer); // 디스크에서 해당 위치의 정보를 blockBuffer 버퍼로 읽어옴

	entry = (EXT2_DIR_ENTRY*)blockBuffer; // 섹터의 시작주소
	entry[location->offset] = *value; // 원하는 값을 기록

	block_write(fs, location->group, location->block, blockBuffer); // 디스크에 blockBuffer 버퍼의 정보를 씀

	return EXT2_ERROR;
}

// inode_num의 데이터 블록에 새로운 엔트리(retEntry) 추가. 성공여부 return (eunseo)
int insert_entry(UINT32 inode_num, EXT2_NODE* retEntry, int fileType)
{
	/*
	인자로 받은 parent의 inode_num을 가지고 해당 아이노드를 읽어온 후 새로운 엔트리를 삽입해주는 함수
	fat에서와 같이 루트 디렉토리, overwrite 등을 생각해 주어야 할 듯(빈 엔트리 찾지 못할 경우,
	 DIR_ENTRY_NO_MORE을 나타내는 디렉토리 엔트리 추가, 위치정보 업데이트 등)
	seungmin */
	// fileType이 overwrite인 경우는 언제 필요한지 모르겠음. 일단 고려하지 않음.

	EXT2_NODE	entryNoMore;			// retEntry 다음 위치에 마지막 엔트리임을 나타내기 위한 노드
	BYTE		entryName[2] = { 0, };	// 엔트리 이름을 저장하는 버퍼
	UINT32		retEntry_inodeNum;		// retEntry의 inode number가 없을 경우 새로 할당 받은 inode number
	DWORD		dataBlockNum;			// 새로 할당 받은 데이터 블록 넘버

	if (GET_INODE_FROM_NODE(retEntry) == 0) // retEntry의 inode number가 없으면
	{
		retEntry_inodeNum = get_free_inode_number(retEntry->fs); // 새로운 inode number 할당
		printf("\n\tretEntry_inodeNum = %d\n", retEntry_inodeNum);
		process_meta_data_for_inode_used(retEntry, retEntry_inodeNum, fileType);
	}

	ZeroMemory(&entryNoMore, sizeof(EXT2_NODE)); // entryNoMore를 0으로 초기화
	entryName[0] = DIR_ENTRY_FREE;
	// 빈 엔트리를 찾아 entryNoMore에 담아옴

	if (lookup_entry(retEntry->fs, inode_num, entryName, &entryNoMore) == EXT2_SUCCESS) // 빈 엔트리가 있다면
	{
		set_entry(retEntry->fs, &entryNoMore.location, &retEntry->entry); // 이 위치에 새 엔트리 기록
		retEntry->location = entryNoMore.location; // 위치정보 기록
	}
	else // 빈 엔트리가 없다면
	{

		// 마지막 엔트리 뒤에 새 엔트리 추가
		entryName[0] = DIR_ENTRY_NO_MORE;
		if (lookup_entry(retEntry->fs, inode_num, entryName, &entryNoMore) == EXT2_ERROR) // 마지막 엔트리를 찾지 못하면
		{
			printf("lookup\n");
			printf("Can't find DIR_ENTRY_NO_MORE\n");
			return EXT2_ERROR;
		}


		set_entry(retEntry->fs, &entryNoMore.location, &retEntry->entry); // 마지막 엔트리를 찾았다면 이 위치에 새 엔트리 기록
		retEntry->location = entryNoMore.location; // 위치정보 기록
		entryNoMore.location.offset++;			   // 마지막 엔트리라고 저장할 위치로 이동

		// 블록에 모든 엔트리가 찼다면
		if (entryNoMore.location.offset == MAX_BLOCK_SIZE / sizeof(EXT2_DIR_ENTRY))
		{
			if (inode_num == 2) // 루트 디렉터리이면
			{
				entryNoMore.location.offset--; // 기존 마지막 엔트리의 위치로 다시 이동
				entryNoMore.entry.name[0] = DIR_ENTRY_NO_MORE; // 마지막 엔트리임을 설정하고
				set_entry(retEntry->fs, &entryNoMore.location, &entryNoMore.entry); // 디스크에 저장

				WARNING("Cannot insert entry into the root entry\n");
				return EXT2_ERROR;
			}

			entryNoMore.location.block++; // 그 다음 블록에 저장
			entryNoMore.location.offset = 0;
			if (expand_block(retEntry->fs, inode_num) == EXT2_ERROR) // 새로운 데이터 블록 할당
				return EXT2_ERROR;
			// process_meta_data_for_block_used(retEntry->fs, retEntry->entry.inode, 0);
		}

		set_entry(retEntry->fs, &entryNoMore.location, &entryNoMore.entry); // 마지막이라고 기록
	}

	return EXT2_SUCCESS;
}

//fs와 아이노드 번호가 들어오면, 아이노드 번호로 블록 그룹을 계산. 계산된 블록 그룹에서 먼저 블록 할당을 하려고함, 블록 그룹에 더이상 할당 가능한 데이터 블럭이 없으면, 인근 블록 그룹에서 블록 가져오는 것으로 하겠음.
UINT32 get_available_data_block(EXT2_FILESYSTEM* fs, UINT32 inode_num)
{
	/*
	사실 선언만 되어 있고 사용되는 곳이 없어서 함수의 정확한 기능을 유추하기는 불가능. 따라서 기능은 정의하기 나름일 것 같음
	함수가 사용되고 있지 않음 -> 정의되지 않은 함수 내에서 사용될 것으로 생각됨 – expand_block 함수에서 사용 될 것으로 예상
	함수명과 인자를 바탕으로 생각해봤을 때 첫 번째 인자로 받은 EXT2_FILESYSTEM 구조체에서 free_block_count를 확인해 할당할 블록이 있는지 확인하고,
	있다면 block_bitmap에서 빈 블록 번호를 return
	expand_block 함수에서는 return 받은 블록 번호를 아이노드의 block 필드의 비어 있는 인덱스에 연결하고, process_meta_data_for_block_used를 통해 메타데이터 수정
	*/
	UINT32 result, inode_which_block_group;	//result : 사용가능한 블록 번호를 저장할 변수, inode_which_block_group : 인자로 받은 아이노드가 어느 블록 그룹에 있는지
	UINT32 block_group_number = 0;			//블럭 그룹 번호 저장
	BYTE block[MAX_BLOCK_SIZE];				//블럭 비트맵을 가져와서 저장할 공간.
	BYTE temp = 0;
	const SECTOR BOOT_BLOCK = 1;			//부트 섹터를 제외한 파일시스템의 기본번지 설정번지에 위치하도록
	EXT2_FILESYSTEM* _fs = fs;
	EXT2_GROUP_DESCRIPTOR* gdp;				//group descriptor pointer라는 뜻
	UINT32 i = 0;
	UINT32 j = 0;
	BYTE	mask = 0xFF;


	if (_fs->sb.free_block_count) //슈퍼블록에서 전체 데이터 블럭에서 빈공간을 탐색, 없으면, 에러 리턴, 있으면 진행.
	{
		inode_which_block_group = GET_INODE_GROUP(inode_num); //아이노드가 속해있는 블럭 그룹 계산
		
		if ((fs->gd.free_blocks_count > 0) && ((fs->sb.block_per_group - fs->gd.free_blocks_count) < MAX_BLOCK_SIZE * 8))	  //아이노드가 속해있는 블럭 그룹에 할당가능한 데이터 블럭이 있는지 확인.
		{
			//아이노드가 있는 블럭 그룹에 할당가능한 데이터블럭이 존재하는 경우 아이노드가 속한 블럭 그룹을 저장.
			block_group_number = inode_which_block_group;
		}
		else //같은 그룹내에 데이터 블럭을 할당할 공간이 없는 경우.
		{
			block_read(fs, 0, GROUP_DES, block);	//0번째 그룹의 블록 디스크립터 테이블 읽어서 block에 저장
			gdp = block;							//block에 맨 처음 주소를 gdp에 할당
			for (i = 0; i < NUMBER_OF_GROUPS; i++)	//사용가능한 아이노드가 없는 경우 값이 0이기 때문에 다음으로 이동. 빈 공간이 있으면 해당 gdp 가지고 나옴
			{
				if ((gdp -> free_blocks_count > 0) && (((fs->sb.block_per_group - gdp->free_blocks_count)) < MAX_BLOCK_SIZE * 8))
					break;
				gdp++;
			}
			
			assert(i != NUMBER_OF_GROUPS);	//끝까지 돌아버렸다는건 사용가능한 데이터 블럭이 없는데 뭔가 잘못됨 정상적이라면 i에 아이노드 빈공간이 있는 그룹 번호가 리턴됨
			block_group_number = i;
		}

		//가장 빨리 비어있는 데이터 블럭 빈자리를 데이터 블럭 비트맵을 통해 구한다.
		ZeroMemory(block, MAX_BLOCK_SIZE);
		block_read(_fs, block_group_number, BLOCK_BITMAP, block);	//블럭 비트맵 읽어옴

		for (i = 0; i < MAX_BLOCK_SIZE; i++)	//i : 비트맵 내에서 오프셋
		{
			if (block[i] != mask)	//block의 i 번째가 0xFF가 아니라면 중간에 빈 공간이 있다는 뜻, if 들어가면 빈공간 찾을 수 있음
			{
				temp = block[i];
				for(j = 0; (j < 8) & ((temp & 1) == 1); j++) //block[i]가 들어간 temp와 1을 and 비트연산 해서 0이면 0이라는 뜻이므로 루프 탈출 아니면 계속 비트 시프트
					temp >>= 1;

				result = (_fs->sb.block_per_group * block_group_number) + (i * 8) + j;	//블럭 번호 계산해서 저장.
				return result;
			}
		}
		return EXT2_ERROR; // 이 루프에서 못 찾은경우 에러 리턴 -> 루프 검증 필요 하다는 뜻
	}
	return EXT2_ERROR; //슈퍼블럭 전체에서 할당가능한 데이터 블럭이 없는 경우 에러 발생.
}

unsigned char toupper(unsigned char ch); //to upper 즉 대문자로 바꾸는 함수 같은데 c 라이브러리에 있는 함수인듯
int isalpha(unsigned char ch);			 //알파벳 확인 함수
int isdigit(unsigned char ch);			 //숫자 확인 함수

void upper_string(char* str, int length) //상위 몇비트를 대문자로 바꾸는 함수
{
	while (*str && length-- > 0)
	{
		*str = toupper(*str);
		str++;
	}
}

int format_name(EXT2_FILESYSTEM* fs, char* name) //파일 이름의 형식이 올바른지 체크하는 함수
{
	UINT32 i, length;
	UINT32 extender = 0, nameLength = 0;
	UINT32 extenderCurrent = 8;
	BYTE regularName[MAX_ENTRY_NAME_LENGTH];

	memset(regularName, 0x20, sizeof(regularName));
	length = strlen(name);

	if (strncmp(name, "..", 2) == 0)
	{
		memcpy(name, "..         ", 11);
		return EXT2_SUCCESS;
	}
	else if (strncmp(name, ".", 1) == 0)
	{
		memcpy(name, ".          ", 11);
		return EXT2_SUCCESS;
	}
	else
	{
		upper_string(name, MAX_ENTRY_NAME_LENGTH);

		for (i = 0; i < length; i++)
		{
			if (name[i] != '.' && !isdigit(name[i]) && !isalpha(name[i]))
				return EXT2_ERROR;

			if (name[i] == '.')
			{
				if (extender)
					return EXT2_ERROR;
				extender = 1;
			}
			else if (isdigit(name[i]) || isalpha(name[i]))
			{
				if (extender)
					regularName[extenderCurrent++] = name[i];
				else
					regularName[nameLength++] = name[i];
			}
			else
				return EXT2_ERROR;
		}

		if (nameLength > 8 || nameLength == 0 || extenderCurrent > 11)
			return EXT2_ERROR;
	}

	memcpy(name, regularName, sizeof(regularName));
	return EXT2_SUCCESS;
}

/*
	같은 이름의 디렉터리 엔트리가 있는지 찾는 함수 (NULL인 경우는 아무 유효한 엔트리를 찾음)
	name이 존재하는 경우 : EXT2_SUCCESS 반환, 찾은 ENTRY로 retEntry가 가리키는 부분 초기화
	name이 존재하지 않는 경우 : EXT2_ERROR 반환
*/
int lookup_entry(EXT2_FILESYSTEM* fs, const int inode, const char* name, EXT2_NODE* retEntry)
{
	INODE	inodeBuffer;
	if (get_inode(fs, inode, &inodeBuffer) == EXT2_ERROR)
		return EXT2_ERROR;

	if (inode == 2) // 루트 디렉터리
		return find_entry_on_root(fs, inodeBuffer, name, retEntry);
	else
		return find_entry_on_data(fs, inodeBuffer, name, retEntry);
}

// 섹터(데이터 블록)에서 formattedName을 가진 엔트리를 찾아 그 위치를 number에 저장
int find_entry_at_block(const BYTE* sector, const BYTE* formattedName, UINT32 begin, UINT32 last, UINT32* number) //sector로 기준점이 들어오면, begin과 last로 섹터 범위를 지정해서 지정된 섹터만큼에서 검색하는걸까
{
	//sector 파라미터는 block으로 생각.
	//섹터 내부의 엔트리를 루프로 돌면서 formattedName과 이름이 같은 엔트리 검색
	//있으면 number변수에 블럭 내에서의 위치를 저장하고, EXT2_SUCCESS 리턴
	EXT2_DIR_ENTRY* dir;
	UINT	i;
	dir = ((EXT2_DIR_ENTRY*)sector + begin);	//디렉토리 엔트리 주소를 sector로 받아서 dir에 저장하고 dir로 이용

	for (i = begin; i <= last; i++)
	{
		if (formattedName == NULL) // 이름에 상관없이 유효한 엔트리의 위치를 찾음
		{
			if ((dir->name[0] != DIR_ENTRY_FREE) && (dir->name[0] != DIR_ENTRY_NO_MORE))
			{
				*number = i;
				return EXT2_SUCCESS;
			}
		}
		else // 찾는 이름이 설정된 경우
		{
			// 삭제된 엔트리나 마지막 엔트리를 찾는 경우 첫번째 바이트만 비교
			if ((formattedName[0] == DIR_ENTRY_FREE || formattedName[0] == DIR_ENTRY_NO_MORE) && (formattedName[0] == dir->name[0]))
			{
				*number = i;
				return EXT2_SUCCESS;
			}

			if (memcmp(dir->name, formattedName, MAX_ENTRY_NAME_LENGTH) == 0)	//비어있는 공간도 아니고 실제로 디렉터리 엔트리가 있으면 이름 비교
			{
				*number = i;				//결과를 찾으면 number에 번호 기록후 리턴
				return  EXT2_SUCCESS;	//EXT2_SUCCESS 리턴
			}
		}

		if (dir->name[0] == DIR_ENTRY_NO_MORE)	//더 이상 디렉터리 엔트리가 없으면. 루프 나옴.
		{
			*number = i;
			return -2;	//섹터 끝까지 보기전에 디렉터리 엔트리끝이 나왔으므로 에러.
		}

		dir++;	//결과 못 찾으면 계속 돔
	}

	*number = i;
	return EXT2_ERROR;	//여기까지 오는건 다 돌았는데 못 찾는 경우이므로 EXT2_ERROR 리턴
}

// 루트 디렉터리 영역에서 formattedName의 엔트리 검색해서 EXT2_NODE* ret에 저장 (eunseo)
int find_entry_on_root(EXT2_FILESYSTEM* fs, INODE inode, char* formattedName, EXT2_NODE* ret)
{
	SECTOR	rootBlock;						// 루트 디렉터리의 첫번째 데이터블록 번호
	BYTE	blockBuffer[MAX_BLOCK_SIZE];	// 루트 디렉터리의 엔트리를 저장하는 섹터
	UINT32	number;							// formattedName을 가진 엔트리가 섹터 내에서 몇번째 엔트리인지
	UINT32	entriesPerBlock, lastEntry;		// entriesPerBlock: 블록 당 엔트리 수, lastEntry: 탐색할 마지막 엔트리
	INT32	result;
	EXT2_DIR_ENTRY* entry;

	rootBlock = get_data_block_at_inode(fs, inode, 1); // 루트 디렉터리의 첫번째 데이터 블록 번호를 return
	block_read(fs, 0, rootBlock, blockBuffer); // 루트 디렉터리의 데이터 블록의 데이터를 blockBuffer에 저장

	entry = (EXT2_DIR_ENTRY*)blockBuffer; // 블록의 시작주소

	entriesPerBlock = MAX_BLOCK_SIZE / sizeof(EXT2_DIR_ENTRY); // 블록 당 엔트리 수 = 32개. sizeof(EXT2_DIR_ENTRY) = 32Byte
	lastEntry = entriesPerBlock - 1; // 탐색할 마지막 엔트리
	result = find_entry_at_block(blockBuffer, formattedName, 0, lastEntry, &number); // blockBuffer에서 formattedName을 가진 엔트리를 찾아 그 위치를 number에 저장

	if (result == -1 || result == -2) // formattedName을 가진 엔트리가 없거나 더 이상 엔트리가 없다면 에러
		return EXT2_ERROR;
	else // 해당 엔트리를 찾았다면 ret에서 가리키는 EXT2_NODE를 entry 정보로 초기화
	{
		memcpy(&ret->entry, &entry[number], sizeof(EXT2_DIR_ENTRY));

		ret->location.group = GET_INODE_GROUP(2);
		ret->location.block = rootBlock;
		ret->location.offset = number; // 블록 안에서의 offset

		ret->fs = fs;
		/*
		block_read(fs, 0, GROUP_DES, blockBuffer);
		memcpy(&(ret->fs->gd), &(((EXT2_GROUP_DESCRIPTOR*)blockBuffer)[GET_INODE_GROUP(entry[number].inode)]), sizeof(EXT2_GROUP_DESCRIPTOR));
		block_read(fs, 0, SUPER_BLOCK, blockBuffer);
		((EXT2_SUPER_BLOCK*)blockBuffer)->block_group_number = GET_INODE_GROUP(entry[number].inode);
		memcpy(&(ret->fs->sb), blockBuffer, sizeof(EXT2_SUPER_BLOCK));*/
	}

	return EXT2_SUCCESS;
}

// 데이터 영역에서 formattedName의 엔트리 검색 (eunseo)
int find_entry_on_data(EXT2_FILESYSTEM* fs, INODE first, const BYTE* formattedName, EXT2_NODE* ret)
{
	BYTE	blockBuffer[MAX_BLOCK_SIZE];	// 엔트리를 저장하는 섹터
	UINT32	blockOffset, number;			// blockOffset: inode 내에서 데이터블록의 위치 오프셋, number: 블록 내에서 formattedName을 가진 엔트리의 위치 오프셋
	UINT32	beginEntry, lastEntry;		// beginEntry: 탐색할 시작 엔트리, lastEntry: 탐색할 마지막 엔트리
	UINT32	entriesPerBlock;			// 블록 당 엔트리 수
	INT32	blockNum;					// 데이터 블록 번호 (그룹에 상관 없이 고유)
	INT32	result;
	EXT2_DIR_ENTRY* entry;

	entriesPerBlock = MAX_BLOCK_SIZE / sizeof(EXT2_DIR_ENTRY); // 블록 당 엔트리 수
	lastEntry = entriesPerBlock - 1; // 마지막 엔트리

	for (blockOffset = 0; blockOffset < first.blocks; blockOffset++) // 데이터 블록 단위로 검색 
	{
		blockNum = get_data_block_at_inode(fs, first, blockOffset + 1); // 데이터 블록 번호 (그룹에 상관 없이 고유)

		block_read(fs, 0, blockNum, blockBuffer); // 데이터 블록의 데이터를 blockBuffer 버퍼에 저장
		entry = (EXT2_DIR_ENTRY*)blockBuffer; // 블록의 시작주소

		beginEntry = blockOffset * entriesPerBlock; // 탐색할 시작 엔트리
		lastEntry = beginEntry + entriesPerBlock - 1; // 탐색할 마지막 엔트리
		result = find_entry_at_block(blockBuffer, formattedName, beginEntry, lastEntry, &number); // blockBuffer에서 formattedName을 가진 엔트리를 찾아 그 위치를 number에 저장

		if (result == -1) // 해당 섹터에 formattedName을 가진 엔트리가 없다면 다음 섹터에서 검색
			continue;
		else // 현재 섹터에서 찾았거나 마지막 엔트리까지 검색한 경우
		{
			if (result == -2) // 더 이상 엔트리가 없다면 에러
				return EXT2_ERROR;
			else // 해당 엔트리를 찾았다면 ret에서 가리키는 EXT2_NODE를 entry 정보로 초기화
			{
				memcpy(&ret->entry, &entry[number], sizeof(EXT2_DIR_ENTRY)); // 엔트리의 내용을 복사

				ret->location.group = 0;
				ret->location.block = blockNum;
				ret->location.offset = number;

				ret->fs = fs;
				/*
				block_read(fs, 0, GROUP_DES, blockBuffer);
				memcpy(&(ret->fs->gd), &(((EXT2_GROUP_DESCRIPTOR*)blockBuffer)[GET_INODE_GROUP(entry[number].inode)]), sizeof(EXT2_GROUP_DESCRIPTOR));
				block_read(fs, 0, SUPER_BLOCK, blockBuffer);
				((EXT2_SUPER_BLOCK*)blockBuffer)->block_group_number = GET_INODE_GROUP(entry[number].inode);
				memcpy(&(ret->fs->sb), blockBuffer, sizeof(EXT2_SUPER_BLOCK));
				*/
			}

			return EXT2_SUCCESS;
		}
	}
}

// inode table에서 inode number에 대한 메타데이터를 inodeBuffer에 저장
int get_inode(EXT2_FILESYSTEM* fs, const UINT32 inode, INODE* inodeBuffer)
// 각 블록 그룹마다 할당되는 아이노드 번호가 정해져 있다고 가정(아이노드 테이블에 저장)
{
	UINT32	groupNumber;					// 해당 아이노드가 속해 있는 블록 그룹 번호
	UINT32	groupOffset;					// 해당 블록 그룹에서의 아이노드 테이블의 위치 + 아이노드 테이블에서 몇 번째 블록인지(블록 단위 offset)
	UINT32	blockOffset;					// 블록에서 몇 번재 아이노드인지
	BYTE	blockBuffer[MAX_BLOCK_SIZE];	// 한 블록을 읽어오기 위한 버퍼

	if (inode > (fs->sb.max_inode_count) || inode < 1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}

	get_inode_location(fs, inode, &groupNumber, &groupOffset, &blockOffset);
	ZeroMemory(blockBuffer, MAX_BLOCK_SIZE);

	if (block_read(fs, groupNumber, groupOffset, blockBuffer))
		// 해당 아이노드가 속해 있는 블록을 읽어옴(data_read 함수에서는 섹터 단위로 탐색하고 섹터 단위로 읽음으로 sectorCount만큼 곱해줌)
		// 섹터 단위로 읽은 후 block에 섹터 단위로 순서대로 저장
		// 부트 섹터는 data_read 함수에서 1로 계산
	{
		return EXT2_ERROR;
	}

	memcpy(inodeBuffer, &(blockBuffer[blockOffset * (fs->sb.inode_structure_size)]), (fs->sb.inode_structure_size));
	// inodeBuffer에 해당 아이노드가 들어있는 메모리만 복사해줌

	return EXT2_SUCCESS;
}

int get_inode_location(EXT2_FILESYSTEM* fs, const UINT32 inode, UINT32* groupNumber, UINT32* groupOffset, UINT32* blockOffset)
{	// 아이노드 번호를 받아서 블록 그룹 번호, 그룹 내 아이노드 테이블+테이블에서의 offset, 블록 내에서의 아이노드 offset을 인자에 저장 by seungmin
	UINT32 inodeTable;
	UINT32 inode_per_block;
	UINT32 tableOffset;

	if (inode > (fs->sb.max_inode_count) || inode < 1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}

	inode_per_block = MAX_BLOCK_SIZE/(fs->sb.inode_structure_size);// 블록 크기에 따라 블록 당 아이노드 수 계산 - 아이노드의 크기를 128byte로 가정함 -> 다른 변수 set할 때도 이게 편할 듯
	*groupNumber = (inode - 1) / (fs->sb.inode_per_group);			// 해당 아이노드가 속해있는 블록그룹의 번호 계산(-1은 아이노드의 인덱스가 1부터 시작하기 때문)
	inodeTable = (fs->gd.start_block_of_inode_table);
	// 해당 블록그룹에서의 아이노드 테이블 시작 위치 -> 수퍼블록에 들어있는 아이노드 테이블의 시작 블록(offset 개념) - 1
	/* 각각의 블록그룹마다 1 block의 수퍼블록, n block의 group_descriptor_table, 1 block의 blcok_bitmap, 1 block의 inode_bitmap을 가지고 있다
	   이 때 group_descriptor_table의 크기는 모두 동일할 것임으로 start_block_of_inode_table을 n+3으로 set해서 offset으로 사용(부트섹터는 data_read에서 더해줌)*/
	   /* -1을 해준 이유 - 블록을 읽을 때는 시작 블록까지 포함에서 읽어야 함으로 첫 블록을 포함시켜 주기 위해*/
	tableOffset = ((inode - 1) % fs->sb.inode_per_group) / inode_per_block;	// 해당 아이노드 테이블에서의 offset(블록 단위)
	*groupOffset = inodeTable + tableOffset;
	*blockOffset = ((inode - 1) % fs->sb.inode_per_group) - (tableOffset * inode_per_block);		// 블록 내 offset(아이노드 개수 단위)

	return EXT2_SUCCESS;
}

int block_read(EXT2_FILESYSTEM* fs, unsigned int group, unsigned int block, unsigned char* blockBuffer)
{	// 인자로 받은 값들을 바탕으로 블록단위로 read  by seungmin
	UINT32 blockSize;							// 블록 사이즈
	UINT32 sectorCount;							// 한 블록 안에 몇 개의 섹터가 들어있는지

	blockSize = MAX_BLOCK_SIZE;	// 블록 사이즈 계산
	sectorCount = blockSize / MAX_SECTOR_SIZE;				// 블록 당 섹터 수 계산

	for (int i = 0; i < sectorCount; i++)
	{
		if (data_read(fs, group, ((block * sectorCount) + i), &(blockBuffer[MAX_SECTOR_SIZE * i])))
		{
			// printf("Read failed\n");
			return EXT2_ERROR;
		}
	}
	return EXT2_SUCCESS;
}

int block_write(EXT2_FILESYSTEM* fs, unsigned int group, unsigned int block, unsigned char* blockBuffer)
{
	UINT32 blockSize;							// 블록 사이즈
	UINT32 sectorCount;							// 한 블록 안에 몇 개의 섹터가 들어있는지

	blockSize = MAX_BLOCK_SIZE;	// 블록 사이즈 계산
	sectorCount = blockSize / MAX_SECTOR_SIZE;			// 블록 당 섹터 수

	for (int i = 0; i < sectorCount; i++)
	{
		if (data_write(fs, group, ((block * sectorCount) + i), &(blockBuffer[MAX_SECTOR_SIZE * i])))
		{
			printf("Write failed\n");
			return EXT2_ERROR;
		}
	}
	return EXT2_SUCCESS;
}

// 루트 디렉터리의 데이터블록을 sector 버퍼에 write (eunseo)
int read_root_block(EXT2_FILESYSTEM* fs, BYTE* sector) //루트 디렉터리에 관한 정보를 읽어옴. fs로 넘겨주면, sector에 담아줌
{
	UINT32 inode = 2;										 // 루트 디렉터리 inode number
	INODE inodeBuffer;										 // 아이노드 메타데이터
	SECTOR rootBlock;										 // 루트 디렉터리의 첫번째 데이터블록 번호
	get_inode(fs, inode, &inodeBuffer);						 // 루트 디렉터리의 메타데이터를 inodeBuffer에 저장
	rootBlock = get_data_block_at_inode(fs, inodeBuffer, 1); // 루트 디렉터리의 첫번째 데이터 블록 번호를 return

	return block_read(fs, 0, rootBlock, sector); // 루트 디렉터리의 데이터 블록의 데이터를 sector 버퍼에 저장
}

int get_data_block_at_inode(EXT2_FILESYSTEM* fs, INODE inode, UINT32 number)	//inode : 어떤 파일의 아이노드, number : inode 구조체의 block필드에서 몇번째 데이터 블록을 불러올지 결정하는 변수 인듯
{
	UINT32 available_block = 0;			//새로 할당할 데이터 블록 번호
	UINT32 new_indirect_block = 0;		//새로 할당할 간접 블록 번호
	UINT32 indirect_boundary = 12;		//간접 블록이 시작하는 경계
	UINT32 entry_num_per_block = MAX_BLOCK_SIZE / 4;
	UINT32 double_indirect_boundary = indirect_boundary + entry_num_per_block;	//이중간접 블록이 시작하는 경계
	UINT32 triple_indirect_boundary = double_indirect_boundary + SQRT(entry_num_per_block);	//삼중간접 블럭이 시작하는 경계
	UINT32 max_block_num = triple_indirect_boundary + TSQRT(entry_num_per_block);

	UINT32 parent_block_num = 0;		//간접 블록 들어갈때 들어가기 전 블럭 번호 저장
	// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수 - 직접 블록 12개 + 간접 블록 + 2중 간접 블록+ 3중 간접 블록
	BYTE block[MAX_BLOCK_SIZE];
	UINT32 recur_num = 0;				//recursion 돌아야 하는 횟수
	UINT32 inode_block_offset = 0;		//간접 블럭을 재귀를 돌며 쉽게 계산하기 위해 number에서 12를 뺀값
	UINT32 offset = 0;					//간접블럭에서의 위치

	if (number < 1 || number > max_block_num)
	{
		printf("Invalid block number\n");
		return EXT2_ERROR;
	}

	number -= 1;	//아이노드가 1번 기준으로 들어오기때문에 계산 쉽게 하기 위해서 0번부터 시작으로 맞춰준다

	if (number < 12)	//직접 블록에 데이터 요청시
	{
		if (inode.block[number] != 0)	//직접 블록에 데이터가 저장되어 있는지 확인
			return inode.block[number];
		return inode_data_empty;	//데이터 할당되어 있지 않다는 뜻으로 비어있다는거 리턴
	}
	else	//간접블록 할당 요청시
	{
		if (number >= triple_indirect_boundary)		//요청한 블럭이 어느 간접 블럭에 있는지 확인, 삼중 간접블럭에 있음
		{
			recur_num = 2;
			parent_block_num = inode.block[14];
		}
		else if (number >= double_indirect_boundary) //요청한 블럭이 어느 간접 블럭에 있는지 확인, 이중 간접블럭에 있음
		{
			recur_num = 1;
			parent_block_num = inode.block[13];
		}
		else if (number >= indirect_boundary)		//요청한 블럭이 어느 간접 블럭에 있는지 확인, 간접블럭에 있음
		{
			recur_num = 0;
			parent_block_num = inode.block[12];
		}
		inode_block_offset = number - 12;						//찾아야하는 block의 offset. recursion할때마다 간접블럭에 맞게 숫자가 계속 바뀜

		while(recur_num >= 0)
		{
			if (parent_block_num == 0)	//부모 블럭이 비어있으면 아이노드 데이터 할당 되어 있지 않다고 리턴
			{
				return inode_data_empty;
			}

			ZeroMemory(block, MAX_BLOCK_SIZE);	//할당되어 있으면 부모 데이터 읽어옴
			block_read(fs, 0, parent_block_num, block);

			switch (recur_num)
			{
			case 0:	//최후 간접블럭에 도달한 경우 offset == 0~255 범위
				if (((UINT32*)block)[inode_block_offset] != 0)	//데이터 블럭이 할당되어 있는 경우
				{
					return ((UINT32*)block)[inode_block_offset];
				}
				return inode_data_empty;
			case 1:	//삼중간접블럭의 이중간접블럭, 아니면 원래 이중간접블럭에 도달한 경우 offset == 
				offset = (inode_block_offset - entry_num_per_block) / entry_num_per_block;
				inode_block_offset = inode_block_offset % entry_num_per_block;
				break;
			case 2:
				offset = (inode_block_offset - entry_num_per_block - SQRT(entry_num_per_block)) / SQRT(entry_num_per_block);
				inode_block_offset = inode_block_offset % SQRT(entry_num_per_block);
				break;
			}
			parent_block_num = ((UINT32*)block)[offset];
			recur_num--;
		}
	}
	return EXT2_ERROR;
}

int ext2_read_superblock(EXT2_FILESYSTEM *fs, EXT2_NODE *root) //슈퍼블록을 읽는 함수 인것 같다. root는 읽은 슈퍼블록을 담을 곳을 인자로 넘겨 받음
{
	INT result;						//결과를 리턴을 위한 변수
	BYTE sector[MAX_BLOCK_SIZE];	//섹터크기 만큼 바이트 설정. 연속적으로 고정된 공간 할당 위해 정적배열 사용 (1024바이트)

	if (fs == NULL || fs->disk == NULL) //fs가 지정되지 않으면 에러
	{
		WARNING("DISK OPERATIONS : %p\nEXT2_FILESYSTEM : %p\n", fs, fs->disk);
		return EXT2_ERROR;
	}

	block_read(fs, 0, SUPER_BLOCK, sector);			   // meta_read -> block_read 	by seungmin
	memcpy(&fs->sb, sector, sizeof(EXT2_SUPER_BLOCK)); // 첫번째 인자가 목적지, 두번째 인자가 어떤 것을 복사할지, 세번째는 크기
	block_read(fs, 0, GROUP_DES, sector);			   // 그룹 디스크립터 읽는듯. 	/ meta_read -> block_read	by seungmin
	memcpy(&fs->gd, sector, sizeof(EXT2_GROUP_DESCRIPTOR));
	//디스크에서 슈퍼블록 정보 입력을 받아와서 인자로 들어온 fs의 슈퍼블록을 업데이트하는 과정으로 생각됨.

	if (fs->sb.magic_signature != 0xEF53) //슈퍼블럭인지 판단하는 필드. 고유값 확인으로.
		return EXT2_ERROR;

	ZeroMemory(sector, sizeof(MAX_BLOCK_SIZE)); //메모리 초기화.
  
	if (read_root_block(fs, sector))			 //슈퍼블록이 루트 디렉터리를 제대로 가리키는지 체크하기 위한 부분이 아닐까 생각.
		return EXT2_ERROR;

	ZeroMemory(root, sizeof(EXT2_NODE));
	memcpy(&root->entry, sector, sizeof(EXT2_DIR_ENTRY)); //앞의 테스트를 거치면 제대로 된 슈퍼블록이므로 root에 담아서 리턴해준다.
	root->fs = fs;

	return EXT2_SUCCESS;
}

UINT32 get_free_inode_number(EXT2_FILESYSTEM* fs) //비어있는 아이노드 번호를 출력하는 것 같다.
{
	//EXT2_FILESYSTEM 구조체를 보면 슈퍼블록 그룹디스크립터 있음.그룹 디스크립터는 비어있는 아이노드 수, 아이노드 테이블 시작 주소, 비트맵 시작주소 가지고 있음
	//일단 아이노드 수 체크해서 없으면 에러, 있으면 비트맵 비교를 통해 가장 앞에 비어있는 아이노드 번호를 리턴 해주어야 한다고 생각.
	//리턴 형이 unsigned int 32비트 형식이니까 그대로 아이노드 번호를 리턴해주어야 할 것으로 생각.

	EXT2_GROUP_DESCRIPTOR* gdp;	//group descriptor pointer라는 뜻
	EXT2_FILESYSTEM* _fs = fs;	//fs가리키는 포인터 새로 생성
	UINT32 result;				//결과를 담을 변수
	BYTE block[MAX_BLOCK_SIZE];	//아이노드 비트맵을 가져와서 저장할 공간.
	BYTE temp = 0;
	UINT32 block_group_number = 0;				//블럭 그룹 번호 저장
	UINT32 j = 0;
	UINT32 i = 0;
	BYTE mask = 0xFF;

	//먼저 슈퍼블럭값을 통해 볼륨 전체에 사용가능한 아이노드 저장공간이 있는지 확인.
	if (_fs->sb.free_inode_count) //볼륨 내에 할당 가능한 아이노드 공간이 있는 경우.
	{
		ZeroMemory(block, MAX_BLOCK_SIZE);
		block_read(fs, 0, GROUP_DES, block);
		gdp = block;
		for (i = 0; i < NUMBER_OF_GROUPS; i++)	//사용가능한 아이노드가 없는 경우 값이 0이기 때문에, 계속 돌게된다. 빈 공간이 있으면 나옴. 처음 블럭그룹부터 탐색
		{
			if (gdp->free_inodes_count > 0)
				break;
			gdp++;
		}
		assert(i != NUMBER_OF_GROUPS);	//끝까지 돌아버렸다는건 사용가능한 아이노드가 없는데 뭔가 잘못됨 정상적이라면 i에 아이노드 빈공간이 있는 그룹 번호가 리턴됨
		block_group_number = i;

		//가장 빨리 비어있는 아이노드 빈자리를 아이노드 비트맵을 통해 구한다.
		ZeroMemory(block, MAX_BLOCK_SIZE);
		block_read(_fs, block_group_number, INODE_BITMAP, block);	//block_group_number 에 비어있는 그룹 번호가 담김 그 그룹의 아이노드 비트맵의 블럭 번호를 읽어옴

		for (i = 0; i < MAX_BLOCK_SIZE; i++)	//i : 비트맵 내에서 오프셋
		{
			if (block[i] != mask)	//block의 i 번째가 0xFF가 아니라면 중간에 빈 공간이 있다는 뜻, if 들어가면 빈공간 찾을 수 있음
			{
				//앞에 그룹 수 * 그룹 내 아이노드 수 + 비트맵 몇번째 섹터 더했는지, 비트맵에서 j가 몇번인지.
				temp = block[i];
				for (j = 0; (j < 8) && ((temp & 1) == 1); j++) //block[i]가 들어간 temp 와 1을 and 비트연산 해서 0이면 0이라는 뜻이므로 루프 탈출 아니면 계속 비트 시프트
				{
					temp >>= 1;
				}

				result = (_fs->sb.inode_per_group * block_group_number) + (i * 8) + j + 1;	//아이노드 번호 계산해서 저장.

				return result;
			}
		}
	}

	return EXT2_ERROR; //볼륨내에 할당가능한 아이노드 공간이 없음.
}


int set_inode_onto_inode_table(EXT2_FILESYSTEM* fs, const UINT32 inode_num, INODE* inode_to_write)	// 인자로 받은 아이노드를 아이노드 번호에 해당하는 위치에 업데이트
{
	UINT32 groupNumber;			// 해당 아이노드의 블록 그룹 번호
	UINT32 groupOffset;			// 해당 아이노드의 그룹 내 offset(블록 단위)
	UINT32 blockOffset;			// 해당 아이노드의 블록 내 offset(아이노드 단위)
	BYTE blockBuffer[MAX_BLOCK_SIZE];			// 블록을 저장할 버퍼

	if (inode_num > fs->sb.max_inode_count || inode_num < 1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}

	if (get_inode_location(fs, inode_num, &groupNumber, &groupOffset, &blockOffset))		// 아이노드의 위치를 찾아 인자에 저장
	{
		return EXT2_ERROR;
	}
	ZeroMemory(blockBuffer, MAX_BLOCK_SIZE);								// 버퍼 초기화
	if (block_read(fs, groupNumber, groupOffset, blockBuffer))							// 해당 아이노드가 속해 있는 블록을 읽어옴
	{
		return EXT2_ERROR;
	}

	memcpy(&(blockBuffer[blockOffset * (fs->sb.inode_structure_size)]), inode_to_write, (fs->sb.inode_structure_size));						// 읽어온 블록에 수정한 아이노드 저장

	if (block_write(fs, groupNumber, groupOffset, blockBuffer))							// 수정한 블록을 다시 업데이트
	{
		return EXT2_ERROR;
	}

	return EXT2_SUCCESS;
	//호출하는 쪽에서 get_free_inode_number을 통해서 비어있는 아이노드 번호를 알아내서 which_inode_num_to_write로 넘겨줄것으로 예상
	//호출하는 쪽에서 새로 생성되는 파일에 대한 아이노드 구조체를 새로 만듬. 그리고 그 구조체를 인자로 넘겨줄것으로 예상됨
	//이 함수에서는 그러면 아이노드 테이블에 아이노드 정보를 기록하고 성공여부를 리턴할 것으로 예상됨.
}

// 디렉터리의 엔트리들을 리스트에 담음
int ext2_read_dir(EXT2_NODE* dir, EXT2_NODE_ADD adder, void* list)
{
	BYTE sector[MAX_BLOCK_SIZE];		// MAX_SECTOR_SIZE -> MAX_BLOCK_SIZE	by seungmin
	INODE inodeBuffer;
	UINT32 inode;
	int i, result, num;

	ZeroMemory(sector, MAX_BLOCK_SIZE);	// MAX_SECTOR_SIZE -> MAX_BLOCK_SIZE	by seungmin
	ZeroMemory(&inodeBuffer, sizeof(INODE));

	result = get_inode(dir->fs, dir->entry.inode, &inodeBuffer); // inode number에 대한 메타데이터를 inodeBuffer에 저장
	if (result == EXT2_ERROR)
		return EXT2_ERROR;

	// 데이터 블록 단위로 루프
	for (i = 0; i < inodeBuffer.blocks; ++i)
	{
		num = get_data_block_at_inode(dir->fs, inodeBuffer, i + 1); // inodeBuffer의 number(i+1)번째 데이터 블록 번호를 return
		block_read(dir->fs, 0, num, sector);						 // 디스크 영역에서 현재 블록그룹의 num번째 데이터 블록의 데이터를 sector 버퍼에 읽어옴
		// data_read -> block_read		by seungmin
		if (dir->entry.inode == 2)									 // 루트 디렉터리
			read_dir_from_sector(dir->fs, sector + 32, adder, list); // 디렉터리 정보를 담은 sector 버퍼를 읽어 엔트리를 list에 추가
			// 여기서 +32는 format, mount 이후에 루트 디렉터리에 알 수 없는 파일 하나가 생기는데, 그 파일을 건너뛰기 위해 주소 크기만큼 더해준 것
		else
			read_dir_from_sector(dir->fs, sector, adder, list); // 디렉터리 정보를 담은 sector 버퍼를 읽어 엔트리를 list에 추가
	}

	return EXT2_SUCCESS;
}

int read_dir_from_sector(EXT2_FILESYSTEM* fs, BYTE* sector, EXT2_NODE_ADD adder, void* list) //블록단위가 아닌 섹터 단위에서 디렉토리 엔트리 리스트들을 가져오는 함수 인듯.
{
	UINT i, max_entries_Per_Sector;
	EXT2_DIR_ENTRY* dir;
	EXT2_NODE node;

	max_entries_Per_Sector = MAX_BLOCK_SIZE / sizeof(EXT2_DIR_ENTRY); //최대 섹터 크기를 디렉터리 엔트리 크기로 나누어서 섹터에 들어갈 수 있는 디렉터리 엔트리 개수를 구한다.
	dir = (EXT2_DIR_ENTRY*)sector;									   //디렉토리 엔트리 주소를 sector로 받아서 dir에 저장하고 dir로 이용
																	  // MAX_SECTOR_SIZE -> MAX_BLOCK_SIZE	by seungmin
	for (i = 0; i < max_entries_Per_Sector; i++)
	{
		if (dir->name[0] == DIR_ENTRY_FREE) //탐색하다가 중간에 비어 있는 공간이 있으면 그냥 통과. fragmentation일 수도 있으니.
			;
		else if (dir->name[0] == DIR_ENTRY_NO_MORE) //더 이상 디렉터리 엔트리가 없으면. 루프 나옴.
			break;
		else //비어있는 공간도 아니고 실제로 디렉터리 엔트리가 있으면 list에 저장하고 다음 엔트리가 있는지 탐색
		{
			node.fs = fs;
			node.entry = *dir;
			adder(fs, list, &node); //adder 함수가 구현되어 있지 않은것 같은데 구현 필요한듯. adder를 하면 list에 node.entry의 정보가 달려오지 않을까.
		}
		dir++;
	}

	return (i == max_entries_Per_Sector ? 0 : -1); //끝가지 돌면 0리턴, 아니면 -1리턴.
}

char* my_strncpy(char* dest, const char* src, int length)
{
	while (*src && *src != 0x20 && length-- > 0)
		*dest++ = *src++;

	return dest;
}

// parent에 새로운 디렉터리 생성
int ext2_mkdir(const EXT2_NODE* parent, const char* entryName, EXT2_NODE* retEntry)
{
	EXT2_NODE dotNode, dotdotNode;
	DWORD firstCluster;
	BYTE name[MAX_NAME_LENGTH];
	BYTE block[MAX_BLOCK_SIZE];
	UINT32 groupNum;
	int result;
	int i;

	strcpy((char*)name, entryName); // 입력한 이름 복사

	if (format_name(parent->fs, (char*)name)) // EXT2 버전의 형식에 맞게 이름 수정
		return EXT2_ERROR;

	/* newEntry */
	ZeroMemory(retEntry, sizeof(EXT2_NODE));
	memcpy(retEntry->entry.name, name, MAX_ENTRY_NAME_LENGTH);		 // name을 복사
	retEntry->entry.name_len = strlen((char*)retEntry->entry.name); // name의 길이 저장
	retEntry->fs = parent->fs;										 // EXT2_FILESYSTEM 복사

	result = insert_entry(parent->entry.inode, retEntry, FILE_TYPE_DIR); // 부모 디렉터리에 새로운 엔트리(retEntry) 추가
	
	if (result == EXT2_ERROR)											 // 에러 발생시 종료
		return EXT2_ERROR;

	expand_block(parent->fs, retEntry->entry.inode); // 새로운 엔트리(retEntry)의 데이터블록 할당
	// process_meta_data_for_block_used(parent->fs, retEntry->entry.inode, 0); // 일단 넣음. 원래는 없었음

	/* dotEntry */
	ZeroMemory(&dotNode, sizeof(EXT2_NODE));
	memset(dotNode.entry.name, 0x20, 11);						  // 이름을 space로 초기화
	dotNode.entry.name[0] = '.';								  // 엔트리 이름 설정 '.'
	dotNode.fs = retEntry->fs;									  // 파일시스템 복사
	dotNode.entry.inode = retEntry->entry.inode;				  // retEntry의 아이노드 복사

	insert_entry(retEntry->entry.inode, &dotNode, FILE_TYPE_DIR); // 새로운 디렉터리(retEntry)에 dotEntry 추가

	/* dotdotEntry */
	ZeroMemory(&dotdotNode, sizeof(EXT2_NODE));
	memset(dotdotNode.entry.name, 0x20, 11); // 이름을 space로 초기화
	dotdotNode.entry.name[0] = '.';			 // 엔트리 이름 설정 '..'
	dotdotNode.entry.name[1] = '.';
	dotdotNode.entry.inode = parent->entry.inode;					 // 부모 디렉터리의 아이노드 복사
	dotdotNode.fs = retEntry->fs;									 // 파일시스템 복사

	insert_entry(retEntry->entry.inode, &dotdotNode, FILE_TYPE_DIR); // 새로운 디렉터리(retEntry)에 dotdotEntry 추가

	//그룹디스크립터 수정: directories_count 변수 수정.				
	groupNum = GET_INODE_GROUP(retEntry->entry.inode); //아이노드 속한 그룹 알아냄
	if (groupNum == 0)
	{
		retEntry->fs->gd.directories_count++;	//그룹디스크립터의 디렉터리 수 증가 후 디스크에도 저장
	}
	ZeroMemory(block, MAX_BLOCK_SIZE);
	block_read(retEntry->fs, groupNum, GROUP_DES, block);	//처음 블럭 그룹의 디스크립터만 수정
	((EXT2_GROUP_DESCRIPTOR*)block)[groupNum].directories_count++;
	block_write(retEntry->fs, groupNum, GROUP_DES, block);

	return EXT2_SUCCESS;
}

int meta_read(EXT2_FILESYSTEM* fs, SECTOR group, SECTOR block, BYTE* sector) //블록 그룹과 블럭을 입력하면 디스크에서 실제 몇번 블럭인지 앞에서 부터 다 계산해서 디스크에서 가져온다.
{
	const SECTOR BOOT_BLOCK = 1;
	SECTOR real_index = BOOT_BLOCK + group * fs->sb.block_per_group + block;

	return fs->disk->read_sector(fs->disk, real_index, sector);
}
int meta_write(EXT2_FILESYSTEM* fs, SECTOR group, SECTOR block, BYTE* sector) //블록 그룹과 블럭을 입력하면 디스크에서 실제 몇번 블럭인지 앞에서 부터 다 계산해서 디스크에 쓴다.
{
	const SECTOR BOOT_BLOCK = 1;
	SECTOR real_index = BOOT_BLOCK + group * fs->sb.block_per_group + block;

	return fs->disk->write_sector(fs->disk, real_index, sector);
}
int data_read(EXT2_FILESYSTEM* fs, SECTOR group, SECTOR sectorNum, BYTE* sector)
{
	UINT32 blockSize;							// 블록 사이즈
	UINT32 sectorCount;							// 한 블록 안에 몇 개의 섹터가 들어있는지
	const SECTOR BOOT_BLOCK = 1;

	blockSize = MAX_BLOCK_SIZE;	// 블록 사이즈 계산
	sectorCount = blockSize / MAX_SECTOR_SIZE;				// 블록 당 섹터 수 계산
	
	SECTOR real_index = ((BOOT_BLOCK + (group * fs->sb.block_per_group)) * sectorCount) + sectorNum;

	return fs->disk->read_sector(fs->disk, real_index, sector); // 성공 여부 리턴 (disksim.c -> disksim_read)
}
int data_write(EXT2_FILESYSTEM* fs, SECTOR group, SECTOR sectorNum, BYTE* sector) // 디스크의 데이터 블록에 작성
{
	UINT32 blockSize;							// 블록 사이즈
	UINT32 sectorCount;							// 한 블록 안에 몇 개의 섹터가 들어있는지
	const SECTOR BOOT_BLOCK = 1;

	blockSize = MAX_BLOCK_SIZE;	// 블록 사이즈 계산
	sectorCount = blockSize / MAX_SECTOR_SIZE;				// 블록 당 섹터 수 계산

	SECTOR real_index = ((BOOT_BLOCK + (group * fs->sb.block_per_group)) * sectorCount) + sectorNum;

	return fs->disk->write_sector(fs->disk, real_index, sector); // 성공 여부 리턴 (disksim.c -> disksim_write)
}

int write_block(DISK_OPERATIONS* disk, UINT32 block_num, const void * block, QWORD sector_per_block)	//fs가 할당되지 않은 상태에서 디스크에 블럭 단위로 기록하는 함수 block_write랑 다름
{
	for (int i = 0; i < sector_per_block; i++)
	{
		if(disk->write_sector(disk, (block_num * sector_per_block) + i, &block[MAX_SECTOR_SIZE * i])) //내부 원리 이해잘 못했음
		{
			printf("Write failed\n");
			return EXT2_ERROR;
		}
	}
	return EXT2_SUCCESS;
}

int read_block(DISK_OPERATIONS* disk, UINT32 block_num, void * block, QWORD sector_per_block)
{
	for (int i = 0; i < sector_per_block; i++)
	{
		if(disk->read_sector(disk, (block_num * sector_per_block) + i, &block[MAX_SECTOR_SIZE * i]))
		{
			printf("Read failed\n");
			return EXT2_ERROR;
		}
	}
	return EXT2_SUCCESS;
}

int ext2_format(DISK_OPERATIONS* disk) //디스크를 ext2파일 시스템으로 초기화 하는 함수
{
	EXT2_SUPER_BLOCK sb;
	EXT2_GROUP_DESCRIPTOR gd;
	EXT2_GROUP_DESCRIPTOR gd_another_group;
	QWORD sector_per_block = MAX_BLOCK_SIZE / MAX_SECTOR_SIZE;				//block 당 섹터가 몇개인지
	QWORD numberOfBlocks = (disk->numberOfSectors) / sector_per_block;		//총 블럭의 개수
	QWORD sector_num_per_group = (disk->numberOfSectors - sector_per_block) / NUMBER_OF_GROUPS; //디스크로부터 디스크 섹터 개수에 대한 정보를 읽어온다. 미리 설정된 그룹 개수만큼 나누어 그룹당 섹터 개수를 계산한다.
	QWORD block_num_per_group = sector_num_per_group / sector_per_block;	//그룹당 블럭의 개수
	QWORD bytesPerBlock = (disk->bytesPerSector) * sector_per_block;
	UINT32 gdPerBlock = MAX_BLOCK_SIZE / sizeof(EXT2_GROUP_DESCRIPTOR);		//block 1024, gd 32byte기준 32개
	int i, gi, j;
	const int BOOT_SECTOR_BASE = 1; //부트 섹터를 제외한 파일시스템의 기본번지 설정번지에 위치하도록
	const int BOOT_BLOCK_BASE = 1;	//disk는 섹터 단위로 계산. BOOT_BLOCK_BASE 는 섹터 베이스 * 블럭당 섹터 수
	char block[MAX_BLOCK_SIZE];
	
	//아래에서 데이터를 넣을 때 모두 블럭 단위로 넣는다. write_block 함수 사용

	// super block
	if (fill_super_block(&sb, numberOfBlocks, bytesPerBlock) != EXT2_SUCCESS) //메모리의 어떤 공간에 슈퍼블록 구조체에 관한 내용을 채워넣는다.
		return EXT2_ERROR;																	//실패시 에러

	ZeroMemory(block, sizeof(block));									//슈퍼블록이 들어갈 메모리 공간을 0으로 초기화 한다.
	memcpy(block, &sb, sizeof(sb));										//아까 슈퍼블록 구조체에 넣어놨던 데이터를 미리 정해둔 크기의 메모리에 올린다.
	write_block(disk, BOOT_BLOCK_BASE + 0, block, sector_per_block);	//disk의 동작을 통해 부트섹터를 제외한 디스크의 가장 처음 블록에 슈퍼블록을 저장한다.

	//Group Descriptor Table
	if (fill_descriptor_block(&gd, &sb, numberOfBlocks, bytesPerBlock) != EXT2_SUCCESS) //슈퍼블록을 디스크에 저장하는 과정과 마찬가지로 디스크에 블록 디스크립터를 저장한다.
		return EXT2_ERROR;

	gd_another_group = gd;
	gd_another_group.free_inodes_count = NUMBER_OF_INODES / NUMBER_OF_GROUPS;
	gd_another_group.free_blocks_count = sb.free_block_count / NUMBER_OF_GROUPS;

	ZeroMemory(block, sizeof(block));
	for (j = 0; j < NUMBER_OF_GROUPS; j++)
	{
		if (j == 0)
			memcpy(block + j * sizeof(gd), &gd, sizeof(gd));
		else
			memcpy(block + j * sizeof(gd_another_group), &gd_another_group, sizeof(gd_another_group));
	}

	write_block(disk, BOOT_BLOCK_BASE + 1, block, sector_per_block);

	// block bitmap
	ZeroMemory(block, sizeof(block));

	block[0] = 0xff;
	block[1] = 0xff;
	block[2] = 0x01;
	write_block(disk, BOOT_BLOCK_BASE + 2, block, sector_per_block);

	// inode bitmap
	ZeroMemory(block, sizeof(block));

	block[0] = 0xff;
	block[1] = 0x07;
	write_block(disk, BOOT_BLOCK_BASE + 3, block, sector_per_block);

	// inode table
	ZeroMemory(block, sizeof(block));

	for (i = 4; i < sb.first_data_block_each_group; i++)
		write_block(disk, BOOT_BLOCK_BASE + i, block, sector_per_block);

	// another group
	for (gi = 1; gi < NUMBER_OF_GROUPS; gi++)
	{
		sb.block_group_number = gi;

		ZeroMemory(block, sizeof(block));
		memcpy(block, &sb, sizeof(sb));

		write_block(disk, block_num_per_group * gi + BOOT_BLOCK_BASE, block, sector_per_block);

		ZeroMemory(block, sizeof(block));
		for (j = 0; j < NUMBER_OF_GROUPS; j++)
		{
			if (j == 0)
				memcpy(block + j * sizeof(gd), &gd, sizeof(gd));
			else
				memcpy(block + j * sizeof(gd_another_group), &gd_another_group, sizeof(gd_another_group));
		}
		write_block(disk, block_num_per_group * gi + BOOT_BLOCK_BASE + 1, block, sector_per_block);

		// block bitmap
		ZeroMemory(block, sizeof(block));
		block[0] = 0xff;
		block[1] = 0xff;
		block[2] = 0x01;
		write_block(disk, block_num_per_group * gi + BOOT_BLOCK_BASE + 2, block, sector_per_block);

		//inode bitmap
		ZeroMemory(block, sizeof(block));
		write_block(disk, block_num_per_group * gi + BOOT_BLOCK_BASE + 3, block, sector_per_block);

		// inode table
		ZeroMemory(block, sizeof(block));
		for (i = 4; i < sb.first_data_block_each_group; i++)
			write_block(disk, block_num_per_group * gi + BOOT_BLOCK_BASE + i, block, sector_per_block);
	}

	PRINTF("max inode count                : %u\n", sb.max_inode_count);
	PRINTF("total block count              : %u\n", sb.block_count);
	PRINTF("byte size of inode structure   : %u\n", sb.inode_structure_size);
	PRINTF("block byte size                : %u\n", MAX_BLOCK_SIZE);
	PRINTF("total sectors count            : %u\n", disk->numberOfSectors);
	PRINTF("sector byte size               : %u\n", disk->bytesPerSector);
	PRINTF("\n");

	create_root(disk, &sb); //루트디렉터리를 만들어 슈퍼블럭과 연결시킨다.

	return EXT2_SUCCESS;
}

int ext2_create(EXT2_NODE* parent, char* entryName, EXT2_NODE* retEntry) //파일시스템에서 파일을 새로 생성할때 호출되는 함수.
{
	if ((parent->fs->gd.free_inodes_count) == 0)
		return EXT2_ERROR; //생성가능한 아이노드 공간이 없으면 에러
  
	UINT32 inode;
	BYTE name[MAX_NAME_LENGTH] = {	0,	};
	BYTE sector[MAX_BLOCK_SIZE];
	int result;

	strcpy(name, entryName);
	if (format_name(parent->fs, name) == EXT2_ERROR)
		return EXT2_ERROR; //이름이 형식에 맞지 않으면 에러

	/* newEntry */
	ZeroMemory(retEntry, sizeof(EXT2_NODE));
	memcpy(retEntry->entry.name, name, MAX_ENTRY_NAME_LENGTH);
	retEntry->fs = parent->fs;
	inode = parent->entry.inode;

	if ((result = lookup_entry(parent->fs, inode, name, retEntry)) == EXT2_SUCCESS)
		return EXT2_ERROR; //이미 디렉터리에 해당 이름의 파일이 있으면 에러
	else if (result == -2)
		return EXT2_ERROR;

	if (insert_entry(inode, retEntry, 0) == EXT2_ERROR)
		return EXT2_ERROR;

	return EXT2_SUCCESS;
}
int ext2_lookup(EXT2_NODE* parent, const char* entryName, EXT2_NODE* retEntry) //entryName을 갖는 엔트리가 있는지 검색해 그 위치를 리턴
{
	EXT2_DIR_ENTRY_LOCATION begin;
	BYTE formattedName[MAX_NAME_LENGTH] = {	0, };

	strcpy(formattedName, entryName);

	if (format_name(parent->fs, formattedName)) //파일이름이 올바른지 체크
		return EXT2_ERROR;

	return lookup_entry(parent->fs, parent->entry.inode, formattedName, retEntry);
}

UINT32 expand_block(EXT2_FILESYSTEM* fs, UINT32 inode_num) // inode에 새로운 데이터블록 할당
{
	INODE inode;
	UINT32 available_block = 0;			//새로 할당할 데이터 블록 번호
	UINT32 new_indirect_block = 0;		//새로 할당할 간접 블록 번호
	UINT32 indirect_boundary = 12;		//간접 블록이 시작하는 경계
	UINT32 entry_num_per_block = MAX_BLOCK_SIZE / 4;	//간접 블럭에 블럭 번호가 몇 개 들어갈 수 있는지
	UINT32 double_indirect_boundary = indirect_boundary + (entry_num_per_block);	//이중간접 블록이 시작하는 경계
	UINT32 triple_indirect_boundary = double_indirect_boundary + SQRT(entry_num_per_block);	//삼중간접 블럭이 시작하는 경계
	UINT32 max_block_num = triple_indirect_boundary + TSQRT(entry_num_per_block);
	UINT32 parent_block_num = 0;		//간접 블록 들어갈때 들어가기 전 블럭 번호 저장
	// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수 - 직접 블록 12개 + 간접 블록 + 2중 간접 블록+ 3중 간접 블록
	UINT32 block[MAX_BLOCK_SIZE/4];		//블럭 저장 UINT32 = 4 * BYTE 할당시 블럭크기에서 4를 나누어주어야 BYTE[MAX_BLOCK_SIZE]와 같은의미
	UINT32 recur_num = 0;				//recursion 돌아야 하는 횟수
	UINT32 inode_block_offset = 0;		//재귀를 쉽게 돌기 위해 하는 것
	UINT32 inode_array_offset = 0;		//block배열에서 어떤 간접 블록 들어가야되는지 가리키는 변수
	UINT32 offset = 0;					//간접블럭에서의 위치

	if (get_inode(fs, inode_num, &inode)) //아이노드 가져오기 실패시 에러 리턴
		return EXT2_ERROR;

	if (inode.blocks < 12)	//지금 아이노드에 데이터 블럭이 몇개 할당되어있는지 계산 12개 미만이면 12개까지 직접블럭이니 through_indirect없이 할당해도 됨.
	{
		available_block = get_available_data_block(fs, inode_num);
		if (available_block == EXT2_ERROR)	//할당가능한 데이터 블럭이 없을때 에러
			return EXT2_ERROR;

		process_meta_data_for_block_used(fs, inode_num, available_block);
		inode.block[inode.blocks] = available_block;	//inode.block 배열에 새로 할당된 데이터 블럭 번호 저장.
		inode.blocks++;									//inode.blocks에 할당된 데이터 블럭 개수 증가.
		set_inode_onto_inode_table(fs, inode_num, &inode);					//아이노드 테이블 업데이트

		return EXT2_SUCCESS;	//할당한 블럭 번호를 리턴
	}
	else	//위에는 직접블록 할당 구간, 여기는 간접블럭 할당 구간
	{
		if (inode.blocks >= triple_indirect_boundary)	//블럭수가 삼중블럭 경계보다 크다는건 삼중 간접 블럭에 들어가야한다는 뜻, 경계에 걸치면 블럭 할당 필요
		{
			recur_num = 2;
			parent_block_num = inode.block[14];
			inode_array_offset = 14;
		}
		else if (inode.blocks >= double_indirect_boundary) //블럭수가 삼중블럭 경계보단 작지만 이중블럭 경계보다 크다는건 이중 간접 블럭에 들어가야한다는 뜻, 경계에 걸치면 블럭 할당 필요
		{
			recur_num = 1;
			parent_block_num = inode.block[13];
			inode_array_offset = 13;
		}
		else if (inode.blocks >= indirect_boundary)	//블럭수가 이중블럭 경계보다 크다는 건 간접 블럭에 들어가야 한다는 뜻, 경계에 걸치면 블럭 할당 필요
		{
			recur_num = 0;
			parent_block_num = inode.block[12];
			inode_array_offset = 12;
		}

		inode_block_offset = inode.blocks - 12;

		if (parent_block_num == 0)	//간접블럭이 할당되어 있지 않은 경우
		{
			new_indirect_block = get_available_data_block(fs, inode_num);
			if (new_indirect_block == EXT2_ERROR)	//할당가능한 데이터 블럭이 없을때 에러
				return EXT2_ERROR;

			parent_block_num = new_indirect_block;
			inode.block[inode_array_offset] = new_indirect_block;					//inode.block 배열에 새로 할당된 데이터 블럭 번호 저장.

			process_meta_data_for_block_used(fs, inode_num, new_indirect_block);	//블럭 사용한다고 기록
			set_inode_onto_inode_table(fs, inode_num, &inode);
		}

		ZeroMemory(block, MAX_BLOCK_SIZE);
		block_read(fs, 0, parent_block_num, block);

		while (recur_num >= 0)
		{
			switch (recur_num)
			{
			case 0:	//간접블럭 다 들어와서 이제 새로운 블럭 할당
				available_block = get_available_data_block(fs, inode_num);	//최종적으로 할당할 블럭
				if (available_block == EXT2_ERROR)	//할당가능한 데이터 블럭이 없을때 에러
					return EXT2_ERROR;

				process_meta_data_for_block_used(fs, inode_num, available_block);
				block[inode_block_offset] = available_block;				//가장 최후의 간접 블록에 사용 표시
				block_write(fs, 0, parent_block_num, block);						//간접 블록 디스크에 저장
				inode.blocks++;
				set_inode_onto_inode_table(fs, inode_num, &inode);					//아이노드 테이블 업데이트

				return EXT2_SUCCESS;	//성공여부 리턴
			case 1:	//이중 간접 블록
				offset = (inode_block_offset - entry_num_per_block) / entry_num_per_block;
				inode_block_offset = inode_block_offset % entry_num_per_block;	//간접 블럭 들어가서 위치
				break;
			case 2:	//삼중 간접 블록
				offset = (inode_block_offset - entry_num_per_block - SQRT(entry_num_per_block)) / SQRT(entry_num_per_block);
				inode_block_offset = inode_block_offset % SQRT(entry_num_per_block);
				break;
			}

			if (block[offset] == 0)	//연결된 이중 간접 데이터블럭이 없다는 뜻으로 할당 해줘야 함
			{
				new_indirect_block = get_available_data_block(fs, inode_num);	//간접 블록 저장위한 블럭 할당받음
				if (new_indirect_block == EXT2_ERROR)				//할당가능한 데이터 블럭이 없을때 에러
					return EXT2_ERROR;

				block[offset] = new_indirect_block;					//새로 할당된 블럭을 부모 블럭에 씀
				block_write(fs, 0, parent_block_num, block);		//부모 블럭 업데이트
				process_meta_data_for_block_used(fs, inode_num, new_indirect_block);	//새롭게 연결한 간접 블럭 사용한다고 기록
			}

			parent_block_num = block[offset];
			block_read(fs, 0, parent_block_num, block);
			recur_num--;
		}
		return EXT2_ERROR;
	}
}

//슈퍼블록 포인터와 섹터 개수 섹터당 바이트 수를 받으면 슈퍼블록 구조체를 채워넣는다.
int fill_super_block(EXT2_SUPER_BLOCK *sb, SECTOR numberOfBlocks, UINT32 bytesPerBlock) 
{
	ZeroMemory(sb, sizeof(EXT2_SUPER_BLOCK));

	sb->max_inode_count = NUMBER_OF_INODES;
	sb->block_count = numberOfBlocks;
	sb->reserved_block_count = 0;
	sb->free_block_count = numberOfBlocks - (17 * NUMBER_OF_GROUPS) - 1;
	sb->free_inode_count = NUMBER_OF_INODES - 11;
	sb->first_data_block = 1;
	switch (bytesPerBlock)
	{
	case 1024:
		sb->log_block_size = 0;
		break;
	case 2048:
		sb->log_block_size = 1;
		break;
	case 4096:
		sb->log_block_size = 2;
		break;
	default:
		printf("BLOCK SIZE ERROR\n");
		break;
	}
	sb->log_fragmentation_size = 0;
	sb->block_per_group = (numberOfBlocks - 1) / NUMBER_OF_GROUPS;
	sb->fragmentation_per_group = 0;
	sb->inode_per_group = NUMBER_OF_INODES / NUMBER_OF_GROUPS;
	sb->magic_signature = 0xEF53;
	sb->errors = 0;
	sb->first_non_reserved_inode = 11;
	sb->inode_structure_size = 128;
	sb->block_group_number = 0;
	sb->first_data_block_each_group = 1 + 1 + 1 + 1 + 13;

	return EXT2_SUCCESS;
}
int fill_descriptor_block(EXT2_GROUP_DESCRIPTOR* gd, EXT2_SUPER_BLOCK* sb, SECTOR numberOfBlocks, UINT32 bytesPerBlock) //그룹 디스크립터 포인터와 슈퍼블록 포인터, 섹터 개수, 섹터당 바이트 수를 받으면 그룹디스크립터 구조체를 채워넣는다.
{
	ZeroMemory(gd, sizeof(EXT2_GROUP_DESCRIPTOR));

	gd->start_block_of_block_bitmap = 2;
	gd->start_block_of_inode_bitmap = 3;
	gd->start_block_of_inode_table = 4;
	gd->free_blocks_count = (UINT32)(sb->free_block_count / NUMBER_OF_GROUPS);
	gd->free_inodes_count = (UINT32)(((sb->free_inode_count) + 11) / NUMBER_OF_GROUPS - 11);
	gd->directories_count = 0;

	return EXT2_SUCCESS;
}
int create_root(DISK_OPERATIONS* disk, EXT2_SUPER_BLOCK* sb) //루트 디렉터리를 생성하는 함수
{
	BYTE sector[MAX_SECTOR_SIZE];	//sector 임시 저장공간
	BYTE block[MAX_BLOCK_SIZE];		//block 임시 저장공간
	SECTOR rootSector = 0;			//루트섹터 위치 표시 블럭과 섹터에서 모두 사용 가능
	EXT2_DIR_ENTRY* entry;			//
	EXT2_GROUP_DESCRIPTOR* gd;		//
	EXT2_SUPER_BLOCK* sb_read;		//
	UINT32 sector_per_block = MAX_BLOCK_SIZE / MAX_SECTOR_SIZE;						//블럭당 섹터 수
	QWORD sector_num_per_group = (disk->numberOfSectors -  sector_per_block) / NUMBER_OF_GROUPS;	//그룹당 섹터 수 1 빼주는 이유 : 부트블럭 섹터 크기 만큼
	QWORD block_num_per_group = sector_num_per_group / sector_per_block;			//그룹당 블럭 수
	UINT32 gdPerBlock = MAX_BLOCK_SIZE / sizeof(EXT2_GROUP_DESCRIPTOR);		//block 1024, gd 32byte기준 32개
	INODE* ip;
	const int BOOT_SECTOR_BASE = 1;
	int gi;

	// entry 초기화
	ZeroMemory(block, MAX_BLOCK_SIZE);
	entry = (EXT2_DIR_ENTRY*)block;

	memcpy(entry->name, VOLUME_LABLE, 11);
	entry->name_len = strlen(VOLUME_LABLE);
	entry->inode = 2; //루트 아이노드의 아이노드 번호는 2번
	entry++;
	entry->name[0] = DIR_ENTRY_NO_MORE;
	rootSector = 1 + sb->first_data_block_each_group;
	write_block(disk, rootSector, block, sector_per_block);

	// super block
	sb_read = (EXT2_SUPER_BLOCK*)block;
	for (gi = 0; gi < NUMBER_OF_GROUPS; gi++)
	{
		read_block(disk, block_num_per_group * gi + BOOT_SECTOR_BASE, block, sector_per_block);
		sb_read->free_block_count--;
		write_block(disk, block_num_per_group * gi + BOOT_SECTOR_BASE, block, sector_per_block);
	}
	sb->free_block_count--;

	// group descriptor
	gd = (EXT2_GROUP_DESCRIPTOR*)block;
	read_block(disk, BOOT_SECTOR_BASE + 1, block, sector_per_block);

	gd->free_blocks_count--;
	gd->directories_count = 1;

	for (gi = 0; gi < NUMBER_OF_GROUPS; gi++)
		write_block(disk, block_num_per_group * gi + BOOT_SECTOR_BASE + 1, block, sector_per_block);

	// data block bitmap
	read_block(disk, BOOT_SECTOR_BASE + 2, block, sector_per_block);
	block[2] |= 0x02;
	write_block(disk, BOOT_SECTOR_BASE + 2, block, sector_per_block);

	// ip 초기화
	ZeroMemory(block, MAX_BLOCK_SIZE);
	ip = (INODE*)block;
	ip++;
	ip->mode = 0x1FF | 0x4000;
	ip->size = 0;
	ip->blocks = 1;
	ip->block[0] = sb->first_data_block_each_group;
	write_block(disk, BOOT_SECTOR_BASE + 4, block, sector_per_block);

	return EXT2_SUCCESS;
}

// block_num번 블록이 할당된 것에 대한 메타데이터 처리 (eunseo)
void process_meta_data_for_block_used(EXT2_FILESYSTEM* fs, UINT32 inode_num, UINT32 block_num)
{
	// EXT2_SUPER_BLOCK *sb;
	BYTE	sector[MAX_BLOCK_SIZE];
	UINT32	i, offset;
	BYTE	mask = 1;
	UINT32	inodeGroupNum = GET_INODE_GROUP(inode_num);
	UINT32	blockGroupNum = (block_num ) / fs->sb.block_per_group;

	// 디스크의 super block과 group descriptor 수정
	for (i = 0; i < NUMBER_OF_GROUPS; i++)
	{
		// 디스크의 sb.free_block_count를 1 감소
		ZeroMemory(sector, sizeof(sector));
		block_read(fs, i, SUPER_BLOCK, sector);
		((EXT2_SUPER_BLOCK*)sector)->free_block_count--;
		block_write(fs, i, SUPER_BLOCK, sector);

		// 디스크의 gd.free_blocks_count 1 감소
		ZeroMemory(sector, sizeof(sector));
		block_read(fs, i, GROUP_DES, sector);
		((EXT2_GROUP_DESCRIPTOR*)sector)[blockGroupNum].free_blocks_count--;
		block_write(fs, i, GROUP_DES, sector);
	}
	
	fs->sb.free_block_count--; // fs의 sb.free_block_count를 1 감소
	if(0 == blockGroupNum)
		fs->gd.free_blocks_count--; // fs의 gd.free_blocks_count를 1 감소

	// Update data block bitmap
	ZeroMemory(sector, sizeof(sector));
	block_read(fs, blockGroupNum, fs->gd.start_block_of_block_bitmap, sector); // 데이터 블록 비트맵 sector 버퍼에 저장
	offset = block_num % 8; // 섹터 내의 offset 계산
	mask <<= offset; // 오프셋을 1로 수정하기 위한 마스크
	sector[((block_num ) % fs->sb.block_per_group) / 8] |= mask; // 비트맵 수정
	block_write(fs, blockGroupNum, fs->gd.start_block_of_block_bitmap, sector); // 디스크에 수정된 비트맵 저장

	return;

	/*
	  처리하다의 process
	  expand_block 등에서 데이터 블록을 할당, 해제했을 때 free_block_count, block_bitmap 등의 메타데이터를 알맞게 수정하는 함수
	  expand_block은 할당의 경우였고 해제의 경우에도 사용될 듯
	  다만 데이터 블록을 수정한 후 이 함수를 호출할 경우 이 함수에서 수정된 아이노드의 번호를가지고 수정내역을 다시 탐색 후
	  메타데이터를 수정해야 할 것 같은데 그럴 바에는 애초에 블록 할당이나 해제하는 함수에서 이 작업도 같이하는 편이 나을 것 같음
	  seungmin */
	  //inode번호로 아이노드 테이블에서 아이노드를 가져옴. 아이노드 데이터블럭에서 가장 마지막 블럭을 비트맵에 사용중이라고 표시 ???
}

void process_meta_data_for_block_free(EXT2_FILESYSTEM* fs, UINT32 inode_num)
{
	EXT2_SUPER_BLOCK sb;
	EXT2_GROUP_DESCRIPTOR* gd;
	INODE	inodeBuffer;
	BYTE	blockBitmap[MAX_BLOCK_SIZE];
	BYTE	block[MAX_BLOCK_SIZE];
	BYTE	zeroBlock[MAX_BLOCK_SIZE];
	BYTE	groupDescriptor[MAX_BLOCK_SIZE];
	INT32	num, bitOffset;
	BYTE	mask = 1;
	UINT32 freeCount = 0;

	UINT32 available_block = 0;			//새로 할당할 데이터 블록 번호
	UINT32 new_indirect_block = 0;		//새로 할당할 간접 블록 번호
	UINT32 indirect_boundary = 12;		//간접 블록이 시작하는 경계
	UINT32 entry_num_per_block = MAX_BLOCK_SIZE / 4;
	UINT32 double_indirect_boundary = indirect_boundary + (entry_num_per_block);	//이중간접 블록이 시작하는 경계
	UINT32 triple_indirect_boundary = double_indirect_boundary + SQRT(entry_num_per_block);	//삼중간접 블럭이 시작하는 경계
	UINT32 max_block_num = triple_indirect_boundary + TSQRT(entry_num_per_block);
	UINT32 parent_block_num = 0;		//간접 블록 들어갈때 들어가기 전 블럭 번호 저장
	// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수 - 직접 블록 12개 + 간접 블록 + 2중 간접 블록+ 3중 간접 블록
	INT32 keep_parent;
	INT32 keep_offset;
	INT32 recur_num = 0;				//recursion 돌아야 하는 횟수
	INT32 inode_block_offset = 0;		//간접 블럭을 재귀를 돌며 쉽게 계산하기 위해 number에서 12를 뺀값
	INT32 offset = 0;					//간접블럭에서의 위치
	UINT32 blockNumber;

	ZeroMemory(groupDescriptor, MAX_BLOCK_SIZE);
	block_read(fs, 0, 1, groupDescriptor);
	gd = groupDescriptor;

	ZeroMemory(&sb, sizeof(EXT2_SUPER_BLOCK));
	block_read(fs, 0, 0, &sb);

	if (get_inode(fs, inode_num, &inodeBuffer) == EXT2_ERROR) // inode number에 대한 메타데이터를 inodeBuffer에 저장
		return;

	num = (inodeBuffer.blocks) - 1;

	while (num >= 0)
	{
		keep_parent = -1;
		keep_offset = -1;
		if (num < 12)
		{
			while (num >= 0 && inodeBuffer.block[num] )
			{
				blockNumber = inodeBuffer.block[num];
				block_read(fs, ((blockNumber )/fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 데이터 블록 비트맵 blockBitmap 버퍼에 저장
				bitOffset = blockNumber % 8; // 섹터 내의 offset 계산
				mask = ~(mask << bitOffset); // 오프셋을 0으로 수정하기 위한 마스크
				blockBitmap[((blockNumber ) % fs->sb.block_per_group) / 8] &= mask; // 비트맵 수정
				block_write(fs, ((blockNumber ) / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 디스크에 수정된 비트맵 저장

				(gd + ((blockNumber ) / fs->sb.block_per_group))->free_blocks_count++;
				if (0 == ((blockNumber ) / fs->sb.block_per_group))
					fs->gd.free_blocks_count--;

				ZeroMemory(zeroBlock, MAX_BLOCK_SIZE);	//데이터 블록 할당 해제 후 초기화.
				block_write(fs, 0, blockNumber, zeroBlock);
				num--;
				freeCount++;
			}
		}
		else	//간접블록 할당 요청시
		{
			if (num >= triple_indirect_boundary)		//요청한 블럭이 어느 간접 블럭에 있는지 확인, 삼중 간접블럭에 있음
			{
				recur_num = 2;
				parent_block_num = inodeBuffer.block[14];
			}
			else if (num >= double_indirect_boundary) //요청한 블럭이 어느 간접 블럭에 있는지 확인, 이중 간접블럭에 있음
			{
				recur_num = 1;
				parent_block_num = inodeBuffer.block[13];
			}
			else if (num >= indirect_boundary)		//요청한 블럭이 어느 간접 블럭에 있는지 확인, 간접블럭에 있음
			{
				recur_num = 0;
				parent_block_num = inodeBuffer.block[12];
			}
			inode_block_offset = num - 12;						//찾아야하는 block의 offset. recursion할때마다 간접블럭에 맞게 숫자가 계속 바뀜

			while (recur_num >= 0)
			{
				if (recur_num == 1 && inode_block_offset < 0)
				{
					parent_block_num = keep_parent;
				}
				else if (recur_num == 2 && offset < 0)
				{
					parent_block_num = inodeBuffer.block[14];
				}

				if (parent_block_num == 0)	//부모 블럭이 비어있으면 아이노드 데이터 할당 되어 있지 않다고 리턴
				{
					printf("The 'blocks' value of INODE is not coincide with allocated block count\n");
					num = -1;
					break;
				}

				ZeroMemory(block, MAX_BLOCK_SIZE);	//할당되어 있으면 부모 데이터 읽어옴
				block_read(fs, 0, parent_block_num, block);

				switch (recur_num)
				{
				case 0:	//최후 간접블럭에 도달한 경우 offset == 0~255 범위
					while (((UINT32*)block)[inode_block_offset] && inode_block_offset >= 0)
					{
						blockNumber = ((UINT32*)block)[inode_block_offset];
						block_read(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 데이터 블록 비트맵 blockBitmap 버퍼에 저장
						bitOffset = blockNumber % 8; // 섹터 내의 offset 계산
						mask = ~(mask << bitOffset); // 오프셋을 0으로 수정하기 위한 마스크
						blockBitmap[(blockNumber % fs->sb.block_per_group) / 8] &= mask; // 비트맵 수정
						block_write(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 디스크에 수정된 비트맵 저장

						(gd + (blockNumber / fs->sb.block_per_group))->free_blocks_count++;

						ZeroMemory(zeroBlock, MAX_BLOCK_SIZE);	//데이터 블록 할당 해제 후 초기화.
						block_write(fs, 0, blockNumber, zeroBlock);
						inode_block_offset--;
						freeCount++;
						num--;
					}
					blockNumber = parent_block_num;
					block_read(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 데이터 블록 비트맵 blockBitmap 버퍼에 저장
					bitOffset = blockNumber % 8; // 섹터 내의 offset 계산
					mask = ~(mask << bitOffset); // 오프셋을 0으로 수정하기 위한 마스크
					blockBitmap[(blockNumber % fs->sb.block_per_group) / 8] &= mask; // 비트맵 수정
					block_write(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 디스크에 수정된 비트맵 저장

					(gd + (blockNumber / fs->sb.block_per_group))->free_blocks_count++;

					ZeroMemory(zeroBlock, MAX_BLOCK_SIZE);	//데이터 블록 할당 해제 후 초기화.
					block_write(fs, 0, blockNumber, zeroBlock);
					freeCount++;

					if (keep_parent == -1)
					{
						recur_num = 0;
						offset = 0;
						break;
					}

					recur_num+=2;
					break;
				case 1:	//삼중간접블럭의 이중간접블럭, 아니면 원래 이중간접블럭에 도달한 경우 offset == 
					if (inode_block_offset < 0)
					{
						inode_block_offset = entry_num_per_block - 1;
						offset--;
						if (offset < 0)
						{
							blockNumber = keep_parent;
							block_read(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 데이터 블록 비트맵 blockBitmap 버퍼에 저장
							bitOffset = blockNumber % 8; // 섹터 내의 offset 계산
							mask = ~(mask << bitOffset); // 오프셋을 0으로 수정하기 위한 마스크
							blockBitmap[(blockNumber % fs->sb.block_per_group) / 8] &= mask; // 비트맵 수정
							block_write(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 디스크에 수정된 비트맵 저장

							(gd + (blockNumber / fs->sb.block_per_group))->free_blocks_count++;

							ZeroMemory(zeroBlock, MAX_BLOCK_SIZE);	//데이터 블록 할당 해제 후 초기화.
							block_write(fs, 0, blockNumber, zeroBlock);
							freeCount++;

							if (keep_offset == -1)
							{
								recur_num = 0;
								offset = 0;
								break;
							}

							recur_num += 2;
						}
						break;
					}

					offset = (inode_block_offset -  entry_num_per_block) /  entry_num_per_block;
					inode_block_offset = inode_block_offset %  entry_num_per_block;
					keep_parent = parent_block_num;
					break;
				case 2:
					if (offset < 0)
					{

						keep_offset--;
						offset = keep_offset;
						inode_block_offset = ((entry_num_per_block - 1) * entry_num_per_block) + entry_num_per_block + (entry_num_per_block - 1);
						if (offset < 0)
						{
							blockNumber = inodeBuffer.block[14];
							block_read(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 데이터 블록 비트맵 blockBitmap 버퍼에 저장
							bitOffset = blockNumber % 8; // 섹터 내의 offset 계산
							mask = ~(mask << bitOffset); // 오프셋을 0으로 수정하기 위한 마스크
							blockBitmap[(blockNumber % fs->sb.block_per_group) / 8] &= mask; // 비트맵 수정
							block_write(fs, (blockNumber / fs->sb.block_per_group), fs->gd.start_block_of_block_bitmap, blockBitmap); // 디스크에 수정된 비트맵 저장

							(gd + (blockNumber / fs->sb.block_per_group))->free_blocks_count++;

							ZeroMemory(zeroBlock, MAX_BLOCK_SIZE);	//데이터 블록 할당 해제 후 초기화.
							block_write(fs, 0, blockNumber, zeroBlock);
							freeCount++;

							recur_num = 0;
							offset = 0;
							break;
						}
					}
					offset = (inode_block_offset -  entry_num_per_block - SQRT( entry_num_per_block)) / SQRT( entry_num_per_block);
					inode_block_offset = inode_block_offset % SQRT( entry_num_per_block);
					keep_offset = offset;
					break;
				}

				parent_block_num = ((UINT32*)block)[offset];
				recur_num--;
			}
		}
	}
	(gd + ((inode_num - 1) / fs->sb.inode_per_group))->free_inodes_count++;
	for (int i = 0; i < NUMBER_OF_GROUPS; i++)
	{
		block_write(fs, i, 1, groupDescriptor);
	}

	sb.free_block_count += freeCount;
	sb.free_inode_count++;
	for (int i = 0; i < NUMBER_OF_GROUPS; i++)
	{
		block_write(fs, i, 0, &sb);
	}
	fs->sb.free_block_count += freeCount;
	fs->sb.free_inode_count++;
	
	// 해제된 아이노드 데이터블럭 0으로 초기화
	for (int i = 0; i < EXT2_N_BLOCKS; i++)
	{
		inodeBuffer.block[i] = 0;
	}
	inodeBuffer.blocks = 0;
	inodeBuffer.size = 0;
	set_inode_onto_inode_table(fs, inode_num, &inodeBuffer);

	return;
}



// Remove file (eunseo)
int ext2_remove(EXT2_NODE* file)
{
	BYTE inodeBuffer[sizeof(INODE)];
	BYTE	blockBuffer[MAX_BLOCK_SIZE];	// 1024Byte
	BYTE	sector[MAX_BLOCK_SIZE];
	int		result, i;
	UINT32	num, offset;				// num: 데이터블록 넘버, offset: 섹터 내에서 데이터블록 오프셋
	UINT16	mask;
	unsigned short fileTypeMask = 0xF000;
	UINT32	groupNum = GET_INODE_GROUP(file->entry.inode);
	
	result = get_inode(file->fs, file->entry.inode, (INODE*)inodeBuffer); // inode number에 대한 메타데이터를 inodeBuffer에 저장
	if (result == EXT2_ERROR)
		return EXT2_ERROR;

	if ((((INODE*)inodeBuffer)->mode & fileTypeMask) && FILE_TYPE_DIR)  // 해당 엔트리가 디렉터리이면 에러
		return EXT2_ERROR;
	
	// 데이터블록 비트맵 수정
	process_meta_data_for_block_free(file->fs, file->entry.inode);

	// 아이노드 비트맵 수정
	ZeroMemory(blockBuffer, MAX_BLOCK_SIZE);
	block_read(file->fs, groupNum, file->fs->gd.start_block_of_inode_bitmap, blockBuffer); // 아이노드 비트맵 blockBuffer 버퍼에 저장
	offset = (file->entry.inode - 1) % 8; // 섹터 내의 offset 계산
	mask = ~(1 << offset); // 오프셋을 1로 수정하기 위한 마스크
	blockBuffer[((file->entry.inode - 1) % (file->fs->sb.inode_per_group)) / 8] &= mask; // 비트맵 수정
	block_write(file->fs, groupNum, file->fs->gd.start_block_of_inode_bitmap, blockBuffer); // 디스크에 수정된 비트맵 저장

	// 삭제된 엔트리라고 저장
	file->entry.name[0] = DIR_ENTRY_FREE;
	set_entry(file->fs, &file->location, &file->entry);

	/*
	1. 아이노드에서 데이터 블록들을 확인해서 연결된 데이터 블록들에 대한 블록 비트맵에 들어가서 해당 블록을 할당가능 상태로 표시해 놓는다.
	2. 왠만하면 아이노드가 있는 같은 블록 그룹에 파일이 저장되지만, 만약 다른 블록 그룹에 있을 경우 해당 블록 그룹의 블록 비트맵에서 할당 가능 표시를 해놓는다.
	3. 아이노드에 표시되어 있는 데이터 블록을 모두 할당 해제 했다면, 아이노드를 나온다.
	4. 삭제할 파일의 아이노드 넘버를 알고 있으면, 아이노드가 있는 블록그룹의 아이노드 테이블에서 아이노드를 삭제하고, 아이노드 비트맵에서 사용가능이라고 표시해놓는다.
	5. 파일과 연결된 부모 디렉터리로 가서, 파일에 대한 디렉터리 엔트리의 이름을 DIR_ENTRY_FREE로 설정하고, 다른 연결을 해제한다.
	+) 동적할당한 것이 없기 때문에 해제하지는 않음
	*/
	return EXT2_SUCCESS;
}

// Read file (eunseo) - offset부터 length만큼 읽어서 buffer에 저장. length = 1024, buffer[1025] = {0,}으로 호출됨
int ext2_read(EXT2_NODE* file, unsigned long offset, unsigned long length, char* buffer)
{
	BYTE	sector[MAX_BLOCK_SIZE];					// 디스크에서 섹터 단위로 읽어오기 위한 버퍼
	DWORD	currentOffset, currentBlock, blockSeq = 0;	// currentOffset: 현재 읽고있는 offset 위치, currentBlock: 현재 읽고있는 데이터블록 번호, blockSeq: 몇번째 블록까지 읽었는지
	DWORD	blockNumber, sectorNumber, sectorOffset;	// blockNumber: 몇번째 블록인지, sectorNumber: 블록 내에서 몇번째 섹터인지, sectorOffset: 섹터 내에서 몇번째 offset인지
	DWORD	readEnd;
	DWORD	blockOffset = 0;
	INODE	node;
	// int		sectorsPerBlock = MAX_SECTOR_SIZE / MAX_BLOCK_SIZE;
	int		i;

	get_inode(file->fs, file->entry.inode, &node); // 읽을 파일의 아이노드 메타데이터를 node에 저장
	currentBlock = node.block[0]; // 시작 블록 번호를 읽어옴
	readEnd = offset + length; // 읽고자 하는 마지막 위치

	currentOffset = offset; // 읽기 시작할 위치 offset
	
	blockOffset = MAX_BLOCK_SIZE; // 블럭 offset은 블럭 크기 단위로 증가
	i = 1;
	while (offset > blockOffset) // 읽고자 하는 위치에 맞게 currentBlock과 blockSeq 조정
	{
		currentBlock = get_data_block_at_inode(file->fs, node, ++i); // node의 i번째 데이터블록 번호
		blockOffset += MAX_BLOCK_SIZE; // blockOffset 증가
		blockSeq++; // 몇번째 블록까지 읽었는지 저장하는 변수 증가
	}

	while (currentOffset < readEnd) // 현재 offset이 읽고자 하는 위치보다 앞쪽인동안
	{
		DWORD	copyLength; // 복사할 데이터의 Byte단위 길이

		blockNumber = currentOffset / MAX_BLOCK_SIZE; // 현재 offset이 몇번째 블록인지 계산
		if (blockSeq != blockNumber) // 다음 블록으로 넘어갔다면
		{
			blockSeq++; // 몇번째 블록까지 읽었는지 저장하는 변수 증가
			++i;
			currentBlock = get_data_block_at_inode(file->fs, node, i); // 다음 블록으로 currentBlock을 변경
		}
		// sectorNumber = (currentOffset / file->fs->disk->bytesPerSector) % sectorsPerBlock; // 블록 내에서 몇번째 섹터인지 계산
		// sectorOffset = currentOffset % file->fs->disk->bytesPerSector; // 섹터 내에서 몇번째 offset인지 계산

		blockOffset = currentOffset % MAX_BLOCK_SIZE;

		if (block_read(file->fs, 0, currentBlock, sector)) // 계산한 위치의 데이터를 섹터단위로 읽음
			break;

		// 현재 읽어야 할 데이터가 마지막 데이터인지 판단. 마지막 데이터가 아니면 전자, 마지막 데이터이면 후자
		copyLength = MIN(MAX_BLOCK_SIZE - blockOffset, readEnd - currentOffset); // 다음 루프에서 버퍼로 복사할 크기

		memcpy(buffer, &sector[blockOffset], copyLength); // 디스크에서 읽어온 데이터를 copyLength만큼 buffer에 복사

		buffer += copyLength; // 다음 데이터를 저장할 위치로 이동
		currentOffset += copyLength; // 다음 데이터를 읽기 위해 offset 조정
	}

	return currentOffset - offset; // 읽은 바이트 수 리턴
}


// Unmount file system
void ext2_umount(EXT2_FILESYSTEM* fs)
{
	return;		//함수 선언부, 인자 받는 부분 수정 필요시 수정해야 될 수도. 일단 fs_umount와 맞춰놓음
}

int ext2_df(EXT2_FILESYSTEM* fs, unsigned int* total, unsigned int* used)
{
	/*
	EXT2_FILESYSTEM의 EXT2_SUPER_BLOCK 필드에서 block_count와 block_count-free_block_count를
	각각 total과 used에 저장(블록사이즈=섹터사이즈라 그냥 넣어도 될 듯)
	*/
	*total = fs->sb.block_count;
	*used = *total - fs->sb.free_block_count;

	return EXT2_SUCCESS;
}

// Remove directory
int ext2_rmdir(EXT2_NODE* dir)
{
	/*
	하위 엔트리 존재 여부 확인하여 에러 처리, 디렉토리인지 확인하여 에러처리(아이노드 등 확인해야 될 듯)
	엔트리의 이름 DIR_ENTRY_FREE로 빈 엔트리로 표시
	메타 데이터 수정(free_block_count 등)
	*/

	EXT2_NODE* _dir = dir;				//EXT2_NODE 를 다른 포인터로 지정해 놓는다.
	INODE dir_inode;					//삭제하려는 디렉터리에 대한 아이노드 정보를 저장하는 변수
	EXT2_DIR_ENTRY* entry;				//디렉터리 엔트리 저장하는 포인터

	BYTE block[MAX_BLOCK_SIZE];			//블럭 비트맵을 가져와서 저장할 공간.
	BYTE temp = 0;						//비트맵 임시 저장 공간
	UINT32 bitmap_idx = 0;				//비트맵 상에서 인덱스 번호
	UINT32 bitmap_offset = 0;			//인덱스에서 몇번 비트인지 offset
	UINT32 entry_num_per_block;			//하나의 블럭에 몇개의 엔트리가 들어갈 수 있는지 저장
	UINT32 inode_table_group_location;	//inode table 저장된 블럭 번호 저장하는 변수.
	UINT32 block_group_number = 0;		//그룹 번호 저장
	UINT32 i = 0;						//다용도
	UINT32 j = 0;						//다용도
	UINT32 num = 0;						//블럭 읽을때 몇번 블럭인지 읽을 때 사용
	entry_num_per_block = MAX_BLOCK_SIZE / sizeof(EXT2_DIR_ENTRY);

	if (!get_inode(_dir->fs, _dir->entry.inode, &dir_inode)) //삭제 하려는 디렉터리의아이노드 정보 읽어오기 EXT2_SUCCESS 값이 0이므로 ! 해줘야 1로 인식됨
	{
		if ((dir_inode.mode & 0xF000) == 0x4000) //mode 비트의 하위 9~11비트를 비교해서 디렉터리인지확인
		{
			if (dir_inode.links_count > 1) //디렉터리를 가르키는 하드링크수가 1개보다 많으면, 디렉터리를 가르키는 링크가 하나 더 있다는 의미이므로 지우려는 디렉터리 엔트리만 삭제
			{
				//아이노드 수정: dir_inode.links_count 하나 줄이기
				block_group_number = GET_INODE_GROUP(_dir->entry.inode); //아이노드 속한 그룹 알아냄
				dir_inode.links_count--;	//아이노드에 연결된 하드링크 수 하나 감소
				set_inode_onto_inode_table(_dir->fs, _dir->entry.inode, &dir_inode); //바뀐 아이노드 정보를 새로 저장.

				//디렉터리엔트리 수정: 디렉터리 엔트리 이름 DIR_ENTRY_FREE로 수정.
				ZeroMemory(block, MAX_BLOCK_SIZE);	//담아올 블럭 공간 초기화
				block_read(_dir->fs, _dir->location.group, _dir->location.block, block);					//디렉터리 엔트리 수정위해 디렉터리 엔트리 들어있는 블럭 읽어옴
				memset(((EXT2_DIR_ENTRY*)block)[_dir->location.offset].name, 0x20, 11);		// 이름을 space로 초기화
				((EXT2_DIR_ENTRY*)block)[_dir->location.offset].name[0] = DIR_ENTRY_FREE;	//디렉터리 엔트리의 이름을 DIR_ENTRY_FREE로 수정
				block_write(_dir->fs, _dir->location.group, _dir->location.block, block);					//디렉터리 엔트리 값을 바꾸고 저장.

				//그룹디스크립터 수정: directories_count 변수 수정.				
				if (block_group_number == 0)
					_dir->fs->gd.directories_count--;	//그룹디스크립터의 디렉터리 수 감소 후 디스크에도 저장
				ZeroMemory(block, MAX_BLOCK_SIZE);
				block_read(_dir->fs, block_group_number, GROUP_DES, block);	//처음 블럭 그룹의 디스크립터만 수정
				((EXT2_GROUP_DESCRIPTOR*)block)[block_group_number].directories_count--;
				block_write(_dir->fs, block_group_number, GROUP_DES, block);
			}
			else //하드링크가 하나 연결되어 있는경우 폴더 완전 삭제를 의미
			{
				if (dir_inode.blocks > 0) //연결된 데이터 블럭이 있으면, 그 데이터 블럭을 탐색해서 데이터 블럭 내의 모든 디렉터리 엔트리가 사용중이지 않은 상태인지 검사. 하나라도 사용중인게 있으면 에러 발생.
				{
					//data block 읽음
					for (j = 0; j < dir_inode.blocks; ++j)
					{
						ZeroMemory(block, MAX_BLOCK_SIZE);
						num = get_data_block_at_inode(_dir->fs, dir_inode, j + 1);	// inodeBuffer의 number(i+1)번째 데이터 블록 번호를 return
						block_read(dir->fs, 0, num, block);							// 디스크 영역에서 현재 블록그룹의 num번째 데이터 블록의 데이터를 block 버퍼에 읽어옴

						entry = (EXT2_DIR_ENTRY*)block;

						//첫번째 블럭이라면 2번 인덱스 부터 읽고 아니면 0번 인덱스부터 읽음 -> . .. 디렉터리 때문에
						for (i = (j > 0) ? 0 : 2; (i < entry_num_per_block) && (entry[i].name[0] != DIR_ENTRY_NO_MORE); i++)
						{
							if (entry[i].name[0] != DIR_ENTRY_FREE) //디렉터리 엔트리가 free가 아니라는건 뭐가 차있다는 것. 에러 발생.
							{
								return EXT2_ERROR;
							}
						}
					}
				}
				//디렉터리의 아이노드의 데이터 블럭이 0이라는건, 할당된 데이터 블럭이 없다는 뜻. 즉 디렉터리안에 아무것도 없으니 그냥 디렉터리 엔트리 지우면된다.
				//여기까지 온거면 연결된게 없다는 뜻 삭제진행

				//inode 연결된 데이터 할당 해제
				process_meta_data_for_block_free(dir->fs, dir->entry.inode);
				get_inode(_dir->fs, _dir->entry.inode, &dir_inode);
				dir_inode.links_count = 0;
				set_inode_onto_inode_table(_dir->fs, _dir->entry.inode, &dir_inode);

				//아이노드 비트맵 수정: 아이노드 비트맵 비트 사용가능 표시
				block_group_number = GET_INODE_GROUP(_dir->entry.inode); //아이노드 속한 그룹 알아냄
				ZeroMemory(block, MAX_BLOCK_SIZE);
				block_read(_dir->fs, block_group_number, INODE_BITMAP, block);	//block에 아이노드가 들어있는 그룹의 아이노드 비트맵 저장.
				bitmap_idx = (((_dir->entry.inode) % (_dir->fs->sb.inode_per_group)) / 8);
				bitmap_offset = (((_dir->entry.inode) % (_dir->fs->sb.inode_per_group)) % 8);
				temp = (~((unsigned)0x01 << bitmap_offset - 1));
				temp &= (block[bitmap_idx]);
				block[bitmap_idx] = temp;
				block_write(_dir->fs, block_group_number, INODE_BITMAP, block); //아이노드 비트맵 값을 바꾼 값으로 변경 후 다시 저장.

				//디렉터리 엔트리 수정: 디렉터리 엔트리 이름 DIR_ENTRY_FREE로 수정.
				ZeroMemory(block, MAX_BLOCK_SIZE);
				block_read(_dir->fs, _dir->location.group, _dir->location.block, block);	//block에 디렉터리 엔트리가 들어있는 블럭 저장.
				memset(((EXT2_DIR_ENTRY*)block)[_dir->location.offset].name, 0x20, 11);		// 이름을 space로 초기화
				((EXT2_DIR_ENTRY*)block)[_dir->location.offset].name[0] = DIR_ENTRY_FREE;	//디렉터리 엔트리의 이름을 DIR_ENTRY_FREE로 수정
				block_write(_dir->fs, _dir->location.group, _dir->location.block, block);	//디렉터리 엔트리 값을 바꾸고 저장.


				//그룹디스크립터 수정: directories_count 감소, free_inode_count 증가
				if (block_group_number == 0)
				{
					_dir->fs->gd.directories_count--;	//그룹디스크립터의 디렉터리 수 감소 후 디스크에도 저장
					_dir->fs->gd.free_inodes_count++;
				}
				ZeroMemory(block, MAX_BLOCK_SIZE);
				block_read(_dir->fs, 0, GROUP_DES, block);	//처음 그룹의 블럭 디스크립터 테이블만 수정
				((EXT2_GROUP_DESCRIPTOR*)block)[block_group_number].directories_count--;
				((EXT2_GROUP_DESCRIPTOR*)block)[block_group_number].free_inodes_count++;
				block_write(_dir->fs, 0, GROUP_DES, block);

				//슈퍼블럭 수정: free_inode_count 증가
				_dir->fs->sb.free_inode_count++;	//슈퍼블럭의 비어있는 아이노드 수 증가.
				ZeroMemory(block, MAX_BLOCK_SIZE);
				block_read(_dir->fs, 0, SUPER_BLOCK, block);	//처음 그룹의 슈퍼블럭만 수정
				((EXT2_SUPER_BLOCK*)block)->free_inode_count++;
				block_write(_dir->fs, 0, SUPER_BLOCK, block);
				

				return EXT2_SUCCESS;
			}
		}
		else
		{
			return EXT2_ERROR; //삭제하려는게 디렉터리가 아닌 경우
		}
	}
	else
	{
		return EXT2_ERROR; //삭제하려는 디렉터리의 아이노드 정보 읽어오기 실패
	}
}